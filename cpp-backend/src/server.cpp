/*
 * server.cpp - POSIX socket server with a worker-thread pool.
 *
 * System calls used: socket, setsockopt, bind, listen, accept4, poll, read,
 *                    write, shutdown, close, pipe (self-pipe wake fd).
 * Concurrency model: N worker threads pull accepted client fds from a
 * mutex+condvar queue; each connection is handled with keep-alive.
 */
#include "server.hpp"

#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "util.hpp"

namespace app {

HttpServer::HttpServer() = default;

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::setWakeFd(int fd) { wakeFd_ = fd; }

void HttpServer::setup(uint16_t port, const std::string& bindAddr) {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) throw std::runtime_error("socket(2) failed");

    int one = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, bindAddr.c_str(), &addr.sin_addr) != 1) {
        ::close(listenFd_);
        throw std::runtime_error("invalid bind address: " + bindAddr);
    }
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::string err = std::string("bind(2) failed: ") + std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        throw std::runtime_error(err);
    }
    if (::listen(listenFd_, 128) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        throw std::runtime_error("listen(2) failed");
    }

    /* Non-blocking accept: the acceptor drains with accept4(...)
     * until EAGAIN, then returns to poll(2) so it can also see the
     * self-pipe shutdown signal. */
    int flags = ::fcntl(listenFd_, F_GETFL, 0);
    if (flags >= 0) ::fcntl(listenFd_, F_SETFL, flags | O_NONBLOCK);
}

void HttpServer::start(int workers) {
    if (listenFd_ < 0) throw std::runtime_error("setup() not called");
    running_ = true;
    for (int i = 0; i < workers; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
    workers_.emplace_back([this] { acceptLoop(); });
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;

    util::safeWrite(STDOUT_FILENO, "[server] joining threads...\n");
    queueCv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();

    util::safeWrite(STDOUT_FILENO, "[server] closing listener...\n");
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }

    /* Close any queued client sockets. */
    std::lock_guard<std::mutex> lock(queueMutex_);
    for (int fd : queue_) ::close(fd);
    queue_.clear();
    util::safeWrite(STDOUT_FILENO, "[server] stopped\n");
}

void HttpServer::acceptLoop() {
    while (running_.load()) {
        pollfd pfd[2];
        pfd[0].fd = listenFd_;
        pfd[0].events = POLLIN;
        pfd[1].fd = wakeFd_;
        pfd[1].events = POLLIN;

        int rc = ::poll(pfd, 2, 500);
        if (!running_.load()) break;
        if (rc <= 0) continue; /* timeout or poll error: re-check running_ */

        /* Wake pipe? -> shutdown requested. */
        if (pfd[1].revents & POLLIN) {
            char drain[64];
            while (::read(wakeFd_, drain, sizeof(drain)) > 0) {}
            break;
        }

        if (!(pfd[0].revents & POLLIN)) continue;

        while (true) {
            int cfd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
            if (cfd < 0) {
                if (errno == EINTR) continue;
                break; /* EAGAIN / transient errors */
            }

            /* TCP_NODELAY: small JSON responses shouldn't wait for Nagle. */
            int one = 1;
            ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                queue_.push_back(cfd);
            }
            queueCv_.notify_one();
        }
    }
}

void HttpServer::workerLoop() {
    for (;;) {
        int cfd = -1;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
            if (!running_.load() && queue_.empty()) return;
            cfd = queue_.back();
            queue_.pop_back();
        }
        if (cfd < 0) continue;

        handleConnection(cfd);
        ::close(cfd);
    }
}

void HttpServer::handleConnection(int fd) {
    active_.fetch_add(1);
    struct ActiveDec {
        std::atomic<size_t>& a;
        ~ActiveDec() { a.fetch_sub(1); }
    } dec{active_};

    std::string buf;
    char chunk[4096];

    while (running_.load()) {
        http::Request req;
        size_t consumed = 0;

        /* Try parsing from what we already have. */
        int pr = http::parseRequest(buf, req, consumed);

        while (pr == 0) { /* need more bytes */
            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;

            int rc = ::poll(&pfd, 1, 10000); /* 10s idle timeout */
            if (rc <= 0) return;             /* timeout or error */

            ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n < 0) {
                if (errno == EINTR) continue;
                return;
            }
            if (n == 0) return; /* peer closed */

            buf.append(chunk, static_cast<size_t>(n));
            if (buf.size() > 16u * 1024 * 1024) return; /* 16 MiB cap */
            pr = http::parseRequest(buf, req, consumed);
        }

        if (pr < 0) {
            http::Response err;
            err.setJson(400, "{\"error\":\"Bad Request\"}");
            err.closeAfter = true;
            err.send(fd);
            return;
        }

        buf.erase(0, consumed);

        totalRequests_.fetch_add(1);
        http::Response res;
        try {
            if (handler) handler(req, res);
        } catch (...) {
            res.setJson(500, "{\"error\":\"Internal Server Error\"}");
        }
        res.send(fd);

        /* HTTP/1.1 defaults to keep-alive; client may ask to close. */
        std::string conn = util::headerKey(req.header("connection"));
        if (res.closeAfter || conn == "close") return;
    }
}

} // namespace app
