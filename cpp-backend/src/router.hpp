/*
 * router.hpp - OOP routing: abstract handler interface + pattern router.
 */
#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <functional>

#include "http.hpp"

namespace app {

struct RouteResult {
    int status = 200;
    std::string body;
    std::string contentType = "application/json; charset=utf-8";
};

/* Abstract base class for all route handlers (OOP / polymorphism). */
class IRouteHandler {
public:
    virtual ~IRouteHandler() = default;
    /* Return true when the route matched and was handled. */
    virtual bool handle(const http::Request& req,
                        const std::map<std::string, std::string>& params,
                        RouteResult& out) = 0;
};

class Router {
public:
    /* e.g. add("POST", "/api/book", handler) */
    void add(const std::string& method, const std::string& pattern,
             std::shared_ptr<IRouteHandler> handler);

    /* Returns false with 404 in `out` when nothing matches; fills 405
     * when the path exists under another method. */
    bool dispatch(const http::Request& req, RouteResult& out);

private:
    /* Split "/api/bookings/:id" into ["api","bookings",":id"]. */
    static std::vector<std::string> segments(const std::string& path);

    struct Route {
        std::string method;
        std::vector<std::string> segs;
        std::shared_ptr<IRouteHandler> handler;
    };
    std::vector<Route> routes_;
};

/* ---------------- Static files ---------------- */

class StaticFileHandler : public IRouteHandler {
public:
    explicit StaticFileHandler(std::string root);

    bool handle(const http::Request& req,
                const std::map<std::string, std::string>& params,
                RouteResult& out) override;

private:
    std::string root_;
};

} // namespace app
