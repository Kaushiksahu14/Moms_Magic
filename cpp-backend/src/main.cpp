/*
 * main.cpp - MOM'S MAGIC booking server (C++ / Linux).
 *
 * Technology showcase:
 *   - Processes : fork(2)/setsid(2) daemon mode, PID file
 *   - Signals   : SIGINT/SIGTERM graceful stop, SIGHUP data reload,
 *                 SIGPIPE ignored, self-pipe trick for prompt shutdown
 *   - Threads   : worker pool (std::thread over POSIX threads)
 *   - Files     : flat POSIX-file storage for bookings (booking.cpp)
 *   - OOP       : IRouteHandler hierarchy, BookingStore class
 *   - STL       : std::vector, std::map, std::string, <algorithm>
 */
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "booking.hpp"
#include "booking_handlers.hpp"
#include "router.hpp"
#include "server.hpp"
#include "util.hpp"

std::time_t startTime = 0;

namespace {

volatile sig_atomic_t g_stop = 0;    /* set by SIGINT/SIGTERM */
volatile sig_atomic_t g_reload = 0;  /* set by SIGHUP */
int g_wakePipe[2] = {-1, -1};

/* Async-signal-safe: record the signal. Only *stop* signals poke the
 * self-pipe: the wake byte means "shut down the acceptor". SIGHUP is
 * picked up by the main loop's short sleep instead. */
extern "C" void onSignal(int sig) {
    if (sig == SIGHUP) {
        g_reload = 1;
        return;
    }
    g_stop = 1;
    const char b = 'x';
    if (g_wakePipe[1] >= 0) {
        ssize_t rc;
        do { rc = ::write(g_wakePipe[1], &b, 1); } while (rc < 0 && errno == EINTR);
    }
}

void installSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGHUP, &sa, nullptr);

    /* Broken client sockets must not kill the server. */
    ::signal(SIGPIPE, SIG_IGN);
}

bool writePidFile(const std::string& path) {
    std::string pid = std::to_string(static_cast<long long>(::getpid())) + "\n";
    return util::writeFile(path, pid);
}

/* Classic double-fork daemonization. */
void daemonize() {
    pid_t pid = ::fork();
    if (pid < 0) {
        std::perror("fork");
        std::exit(1);
    }
    if (pid > 0) {
        std::printf("[moms-magic] daemonized, child pid %d\n", static_cast<int>(pid));
        std::exit(0); /* parent goes away */
    }
    if (::setsid() < 0) {
        std::perror("setsid");
        std::exit(1);
    }
    /* Second fork: can never reacquire a controlling terminal. */
    pid = ::fork();
    if (pid < 0) std::exit(1);
    if (pid > 0) std::exit(0);

    ::umask(022);
    if (::chdir("/") < 0) { /* data/webroot paths were made absolute earlier */ }

    int nullFd = ::open("/dev/null", O_RDWR);
    if (nullFd >= 0) {
        ::dup2(nullFd, STDIN_FILENO);
        ::dup2(nullFd, STDOUT_FILENO);
        ::dup2(nullFd, STDERR_FILENO);
        if (nullFd > STDERR_FILENO) ::close(nullFd);
    }
}

void printUsage(const char* argv0) {
    std::printf(
        "MOM'S MAGIC booking server (C++/Linux)\n"
        "Usage: %s [--port N] [--data DIR] [--root DIR] [--daemon] [--help]\n"
        "  --port N    TCP port to listen on   (default 5000)\n"
        "  --data DIR  booking data directory  (default ./data)\n"
        "  --root DIR  static web root         (default .. = project root)\n"
        "  --daemon    fork into the background\n",
        argv0);
}

} // namespace

int main(int argc, char** argv) {
    uint16_t port = 5000;
    std::string dataDir = "data";
    std::string webRoot = ".."; /* project root: index.html, admin.html, ... */
    bool daemonMode = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (a == "--data" && i + 1 < argc) {
            dataDir = argv[++i];
        } else if (a == "--root" && i + 1 < argc) {
            webRoot = argv[++i];
        } else if (a == "--daemon") {
            daemonMode = true;
        } else if (a == "--help" || a == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }
    if (port == 0) {
        std::fprintf(stderr, "invalid port\n");
        return 1;
    }

    startTime = ::time(nullptr);

    /* Resolve paths to absolute *before* daemonizing (daemon chdirs to /). */
    std::error_code ec;
    std::filesystem::path absData = std::filesystem::absolute(dataDir, ec);
    if (!ec) dataDir = absData.string();
    std::filesystem::path absRoot = std::filesystem::absolute(webRoot, ec);
    if (!ec) webRoot = absRoot.string();

    if (daemonMode) daemonize();

    installSignalHandlers();
    if (::pipe(g_wakePipe) < 0) {
        std::perror("pipe");
        return 1;
    }

    util::safeWrite(STDOUT_FILENO,
                    ("[moms-magic] starting on port " + std::to_string(port) +
                     " (pid " + std::to_string(static_cast<long long>(::getpid())) + ")\n").c_str());

    /* ---- Data store (file handling) ---- */
    app::BookingStore store(dataDir);
    if (!store.load()) {
        util::safeWrite(STDOUT_FILENO, "[fatal] cannot prepare data directory\n");
        return 1;
    }
    if (!writePidFile(util::joinPath(dataDir, "server.pid"))) {
        util::safeWrite(STDOUT_FILENO, "[warn] could not write pid file\n");
    }

    /* ---- HTTP server (threads + sockets) ---- */
    app::HttpServer server;
    server.setWakeFd(g_wakePipe[0]);

    /* ---- Router (OOP handlers) ---- */
    auto router = std::make_shared<app::Router>();
    router->add("POST",   "/api/book",         std::make_shared<app::BookCreateHandler>(store));
    router->add("GET",    "/api/bookings",     std::make_shared<app::BookingListHandler>(store));
    router->add("DELETE", "/api/bookings/:id", std::make_shared<app::BookingDeleteHandler>(store));
    router->add("GET",    "/api/status",       std::make_shared<app::StatusHandler>(store, server));

    app::StaticFileHandler staticFiles(webRoot);

    server.handler = [router, &staticFiles](const http::Request& req, http::Response& res) {
        app::RouteResult rr;
        bool matched = router->dispatch(req, rr);
        if (matched) {
            res.status = rr.status;
            res.contentType = rr.contentType;
            res.body = std::move(rr.body);
        } else if (req.path.rfind("/api/", 0) == 0) {
            /* Unknown API path: JSON 404/405 from the router. */
            res.status = rr.status;
            res.contentType = rr.contentType;
            res.body = std::move(rr.body);
        } else {
            /* Fall back to static files so the website keeps working. */
            app::RouteResult sf;
            staticFiles.handle(req, {}, sf);
            res.status = sf.status;
            res.contentType = sf.contentType;
            res.body = std::move(sf.body);
        }
        app::logRequest(req, res.status);
    };

    try {
        server.setup(port);
        server.start(8);
    } catch (const std::exception& e) {
        std::string msg = std::string("[fatal] ") + e.what() + "\n";
        util::safeWrite(STDOUT_FILENO, msg.c_str());
        return 1;
    }

    util::safeWrite(STDOUT_FILENO, "[moms-magic] ready; Ctrl+C to stop, SIGHUP to reload data\n");

    /* ---- Main loop: wait for signals ---- */
    while (!g_stop) {
        struct timespec ts{0, 200 * 1000 * 1000}; /* 200 ms */
        if (nanosleep(&ts, nullptr) < 0 && errno != EINTR) break;

        if (g_reload) {
            g_reload = 0;
            /* SIGHUP: re-read the data file without dropping connections. */
            util::safeWrite(STDOUT_FILENO, "[moms-magic] SIGHUP: reloading bookings\n");
            store.load();
        }
    }

    util::safeWrite(STDOUT_FILENO, "[moms-magic] shutting down...\n");
    server.stop();
    util::safeWrite(STDOUT_FILENO, "[moms-magic] bye\n");
    return 0;
}
