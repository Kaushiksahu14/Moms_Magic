/*
 * booking_handlers.cpp - Booking API implementation.
 */
#include "booking_handlers.hpp"

#include <ctime>
#include <unistd.h>

#include "json.hpp"
#include "util.hpp"

/* Defined in main.cpp; start time of the daemon for uptime reporting. */
extern std::time_t startTime;

namespace app {

void logRequest(const http::Request& req, int status) {
    std::string line = util::formatTime(::time(nullptr)) + "  " +
                       req.method + " " + req.path +
                       " -> " + std::to_string(status) + "\n";
    util::safeWrite(STDOUT_FILENO, line.c_str());
}

/* ---------------- POST /api/book ---------------- */

bool BookCreateHandler::handle(const http::Request& req,
                               const std::map<std::string, std::string>& /*params*/,
                               RouteResult& out) {
    if (req.method != "POST") {
        out.status = 405;
        out.body = "{\"error\":\"Method Not Allowed\"}";
        return true;
    }

    json::Value root;
    std::string err;
    if (!json::Value::parse(req.body, root, err) || !root.isObject()) {
        out.status = 400;
        out.body = "{\"message\":\"Invalid JSON body\"}";
        return true;
    }

    std::string name    = util::trim(json::getString(root, "name"));
    std::string email   = util::trim(json::getString(root, "email"));
    std::string datetime = util::trim(json::getString(root, "datetime"));
    long long people    = json::getInt(root, "people", 0);
    std::string message = json::getString(root, "message");

    if (name.empty() || email.empty() || datetime.empty()) {
        out.status = 400;
        out.body = "{\"message\":\"Missing required fields: name, email, datetime\"}";
        return true;
    }
    if (people <= 0 || people > 20) {
        out.status = 400;
        out.body = "{\"message\":\"Invalid number of people\"}";
        return true;
    }

    Booking created;
    int bookedInSlot = 0, seatsLeft = 0;
    if (!store_.tryCreate(name, email, datetime, static_cast<int>(people),
                          message, created, bookedInSlot, seatsLeft)) {
        if (bookedInSlot < 0) {
            /* Invalid datetime string. */
            out.status = 400;
            out.body = "{\"message\":\"Invalid datetime\"}";
        } else {
            out.status = 400;
            out.body = "{\"message\":\"This time slot is full. Please choose another time.\"}";
        }
        return true;
    }

    json::Value resp = json::Value::object();
    resp.set("message", "Booking Confirmed");
    json::Value bj = json::Value::object();
    json::Value::parse(created.toJson(), bj, err);
    resp.set("booking", bj);
    out.status = 200;
    out.body = resp.dump();
    return true;
}

/* ---------------- GET /api/bookings ---------------- */

bool BookingListHandler::handle(const http::Request& req,
                                const std::map<std::string, std::string>& /*params*/,
                                RouteResult& out) {
    if (req.method != "GET") {
        out.status = 405;
        out.body = "{\"error\":\"Method Not Allowed\"}";
        return true;
    }
    std::string body = "[";
    const auto all = store_.listAll();
    for (size_t i = 0; i < all.size(); ++i) {
        if (i) body += ",";
        body += all[i].toJson();
    }
    body += "]";
    out.status = 200;
    out.body = std::move(body);
    return true;
}

/* ---------------- DELETE /api/bookings/:id ---------------- */

bool BookingDeleteHandler::handle(const http::Request& req,
                                  const std::map<std::string, std::string>& params,
                                  RouteResult& out) {
    if (req.method != "DELETE") {
        out.status = 405;
        out.body = "{\"error\":\"Method Not Allowed\"}";
        return true;
    }
    auto it = params.find("id");
    if (it == params.end() || it->second.empty()) {
        out.status = 400;
        out.body = "{\"error\":\"Missing id\"}";
        return true;
    }
    if (!store_.remove(it->second)) {
        out.status = 404;
        out.body = "{\"error\":\"Booking not found\"}";
        return true;
    }
    out.status = 200;
    out.body = "{\"message\":\"Booking deleted\"}";
    return true;
}

/* ---------------- GET /api/status ---------------- */

bool StatusHandler::handle(const http::Request& req,
                           const std::map<std::string, std::string>& /*params*/,
                           RouteResult& out) {
    if (req.method != "GET") {
        out.status = 405;
        out.body = "{\"error\":\"Method Not Allowed\"}";
        return true;
    }
    json::Value v = json::Value::object();
    v.set("service", "moms-magic-cpp");
    v.set("status", "ok");
    v.set("bookings", static_cast<long long>(store_.count()));
    v.set("totalRequests", static_cast<long long>(server_.totalRequests()));
    v.set("activeConnections", static_cast<long long>(server_.activeConnections()));
    v.set("workers", static_cast<long long>(server_.workerCount()));
    v.set("pid", static_cast<long long>(static_cast<long long>(getpid())));
    v.set("uptimeSec", static_cast<long long>(::time(nullptr) - ::startTime));
    out.status = 200;
    out.body = v.dump();
    return true;
}

} // namespace app
