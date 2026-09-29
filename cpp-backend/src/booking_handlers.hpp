/*
 * booking_handlers.hpp - Concrete route handlers for the booking API.
 */
#pragma once

#include "router.hpp"
#include "booking.hpp"
#include "server.hpp"

namespace app {

/* POST /api/book  {name,email,datetime,people,message} */
class BookCreateHandler : public IRouteHandler {
public:
    explicit BookCreateHandler(BookingStore& store) : store_(store) {}
    bool handle(const http::Request& req,
                const std::map<std::string, std::string>& params,
                RouteResult& out) override;
private:
    BookingStore& store_;
};

/* GET /api/bookings -> newest-first JSON array */
class BookingListHandler : public IRouteHandler {
public:
    explicit BookingListHandler(BookingStore& store) : store_(store) {}
    bool handle(const http::Request& req,
                const std::map<std::string, std::string>& params,
                RouteResult& out) override;
private:
    BookingStore& store_;
};

/* DELETE /api/bookings/:id */
class BookingDeleteHandler : public IRouteHandler {
public:
    explicit BookingDeleteHandler(BookingStore& store) : store_(store) {}
    bool handle(const http::Request& req,
                const std::map<std::string, std::string>& params,
                RouteResult& out) override;
private:
    BookingStore& store_;
};

/* GET /api/status -> server + store stats */
class StatusHandler : public IRouteHandler {
public:
    StatusHandler(BookingStore& store, HttpServer& server)
        : store_(store), server_(server) {}
    bool handle(const http::Request& req,
                const std::map<std::string, std::string>& params,
                RouteResult& out) override;
private:
    BookingStore& store_;
    HttpServer& server_;
};

/* Shared request logging (worker thread context). */
void logRequest(const http::Request& req, int status);

} // namespace app
