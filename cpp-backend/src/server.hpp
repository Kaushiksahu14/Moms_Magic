/*
 * server.hpp - Multithreaded HTTP/1.1 server on POSIX sockets.
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cstdint>
#include <cstddef>

#include "http.hpp"
#include "router.hpp"

namespace app {

class HttpServer {
public:
    using HandlerFn = std::function<void(const http::Request&, http::Response&)>;

    HttpServer();
    ~HttpServer();

    /* Create, bind(2), listen(2). Throws std::runtime_error on failure. */
    void setup(uint16_t port, const std::string& bindAddr = "0.0.0.0");

    /* Spawn worker threads + acceptor. Non-blocking. */
    void start(int workers = 8);

    /* Wake acceptor via self-pipe and join everything. */
    void stop();

    /* Unix domain socket to wake acceptor for shutdown. */
    void setWakeFd(int fd);

    /* Numbers for the /api/status endpoint. */
    size_t totalRequests() const { return totalRequests_.load(); }
    size_t activeConnections() const { return active_.load(); }
    size_t workerCount() const { return workers_.size(); }

    /* Set by main; called for every request. */
    HandlerFn handler;

private:
    void acceptLoop();
    void workerLoop();
    void handleConnection(int fd);

    int listenFd_ = -1;
    int wakeFd_ = -1;                    /* read end of self-pipe */
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> totalRequests_{0};
    std::atomic<size_t> active_{0};

    /* Simple blocking queue of client fds. */
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::vector<int> queue_;
};

} // namespace app
