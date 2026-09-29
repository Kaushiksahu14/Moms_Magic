/*
 * http.hpp - HTTP/1.1 request parsing and response building (POSIX sockets).
 */
#pragma once

#include <string>
#include <map>
#include <cstdint>

namespace http {

struct Request {
    std::string method;                 /* raw request target, e.g. /api/book */
    std::string target;
    std::string path;                   /* target without query string */
    std::string query;                  /* raw query string (no '?') */
    std::map<std::string, std::string> headers; /* keys lower-cased */
    std::string body;

    /* header(name) is case-insensitive; "" when absent. */
    std::string header(const std::string& name) const;
};

class Response {
public:
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    bool closeAfter = false;

    void setJson(int code, const std::string& jsonBody);
    void setHtml(int code, const std::string& htmlBody);

    /* Serialize status line + headers + body. */
    std::string serialize() const;

    /* Write to fd; handles partial writes and SIGPIPE-safe failure. */
    bool send(int fd) const;

    static const char* statusText(int code);

private:
    bool writeAll(int fd, const char* data, size_t len) const;
};

/* Parse one request from buf.
 * Returns:
 *   1  -> full request parsed into req (consumedUpTo set)
 *   0  -> need more bytes
 *  -1  -> malformed request
 * consumedUpTo: how many bytes of buf belong to this request.
 */
int parseRequest(const std::string& buf, Request& req, size_t& consumedUpTo);

} // namespace http
