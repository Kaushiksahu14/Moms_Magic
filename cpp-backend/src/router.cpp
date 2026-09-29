/*
 * router.cpp - Route table matching and static file serving.
 * System calls used: open/read/close via util::readFile, stat via fstat.
 */
#include "router.hpp"

#include "json.hpp"
#include "util.hpp"

namespace app {

/* ---------------- Router ---------------- */

std::vector<std::string> Router::segments(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void Router::add(const std::string& method, const std::string& pattern,
                 std::shared_ptr<IRouteHandler> handler) {
    Route r;
    r.method = util::toLower(util::trim(method));
    r.segs = segments(pattern);
    r.handler = std::move(handler);
    routes_.push_back(std::move(r));
}

bool Router::dispatch(const http::Request& req, RouteResult& out) {
    std::vector<std::string> reqSegs = segments(req.path);
    std::string reqMethod = util::toLower(req.method);
    bool pathMatched = false;

    for (const Route& r : routes_) {
        if (r.segs.size() != reqSegs.size()) continue;

        std::map<std::string, std::string> params;
        bool ok = true;
        for (size_t i = 0; i < r.segs.size(); ++i) {
            const std::string& p = r.segs[i];
            if (!p.empty() && p[0] == ':') {
                params[p.substr(1)] = util::urlDecode(reqSegs[i]);
            } else if (!util::headerKey(p).empty() &&
                       util::headerKey(p) != util::headerKey(reqSegs[i])) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        pathMatched = true;

        if (r.method != reqMethod) continue;

        if (r.handler->handle(req, params, out)) return true;
    }

    if (pathMatched) {
        out.status = 405;
        out.body = "{\"error\":\"Method Not Allowed\"}";
        out.contentType = "application/json; charset=utf-8";
    } else {
        out.status = 404;
        out.body = "{\"error\":\"Not Found\"}";
        out.contentType = "application/json; charset=utf-8";
    }
    return false;
}

/* ---------------- Static files ---------------- */

namespace {

std::string mimeType(const std::string& path) {
    size_t dot = path.rfind('.');
    std::string ext = (dot == std::string::npos) ? "" : util::toLower(path.substr(dot + 1));
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")  return "text/css; charset=utf-8";
    if (ext == "js")   return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")  return "image/gif";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "ico")  return "image/x-icon";
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf")  return "font/ttf";
    if (ext == "txt")  return "text/plain; charset=utf-8";
    if (ext == "mp4")  return "video/mp4";
    if (ext == "webm") return "video/webm";
    return "application/octet-stream";
}

} // namespace

StaticFileHandler::StaticFileHandler(std::string root) : root_(std::move(root)) {}

bool StaticFileHandler::handle(const http::Request& req,
                               const std::map<std::string, std::string>& /*params*/,
                               RouteResult& out) {
    if (req.method != "GET" && req.method != "HEAD") {
        out.status = 405;
        out.body = "{\"error\":\"Method Not Allowed\"}";
        return true;
    }

    /* Decode and sanitize the URL path. */
    std::string rel = util::urlDecode(req.path);
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    if (rel.empty()) rel = "index.html";

    /* Reject traversal and hidden files. */
    if (rel.find("..") != std::string::npos ||
        rel.find('\\') != std::string::npos ||
        rel.find("/.") != std::string::npos ||
        rel.front() == '.') {
        out.status = 403;
        out.body = "{\"error\":\"Forbidden\"}";
        return true;
    }

    std::string full = util::joinPath(root_, rel);
    std::string data;
    if (!util::readFile(full, data)) {
        out.status = 404;
        out.body = "{\"error\":\"Not Found\"}";
        return true;
    }

    out.status = 200;
    out.contentType = mimeType(rel);
    out.body = std::move(data);
    return true;
}

} // namespace app
