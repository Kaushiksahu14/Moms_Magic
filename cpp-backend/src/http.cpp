/*
 * http.cpp - HTTP request parsing and response sending.
 * System calls used: write (via util::safeWrite style loop), close by caller.
 */
#include "http.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <unistd.h>

#include "util.hpp"

namespace http {

/* ---------------- Request ---------------- */

std::string Request::header(const std::string& name) const {
    auto it = headers.find(util::headerKey(name));
    return it == headers.end() ? "" : it->second;
}

int parseRequest(const std::string& buf, Request& req, size_t& consumedUpTo) {
    /* Need full header block? */
    size_t headerEnd = buf.find("\r\n\r\n");
    size_t sepLen = 4;
    if (headerEnd == std::string::npos) {
        headerEnd = buf.find("\n\n");
        sepLen = 2;
        if (headerEnd == std::string::npos) return 0;
    }

    std::string head = buf.substr(0, headerEnd);
    std::string rest = buf.substr(headerEnd + sepLen);

    /* Request line. */
    size_t lineEnd = head.find('\n');
    std::string requestLine = head.substr(0, lineEnd);
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

    std::vector<std::string> parts = util::split(requestLine, ' ');
    /* Remove empty tokens (defensive against double spaces). */
    parts.erase(std::remove(parts.begin(), parts.end(), ""), parts.end());
    if (parts.size() != 3) return -1;

    req.method = parts[0];
    req.target = parts[1];

    size_t qpos = req.target.find('?');
    if (qpos == std::string::npos) {
        req.path = req.target;
        req.query.clear();
    } else {
        req.path = req.target.substr(0, qpos);
        req.query = req.target.substr(qpos + 1);
    }

    /* Headers. */
    req.headers.clear();
    if (lineEnd != std::string::npos) {
        size_t pos = lineEnd + 1;
        while (pos < head.size()) {
            size_t eol = head.find('\n', pos);
            std::string line = head.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pos = (eol == std::string::npos) ? head.size() : eol + 1;

            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = util::headerKey(line.substr(0, colon));
            std::string val = util::trim(line.substr(colon + 1));
            if (!key.empty()) req.headers[key] = val;
        }
    }

    /* Body length. */
    size_t contentLength = 0;
    std::string cl = req.header("content-length");
    if (!cl.empty()) {
        char* end = nullptr;
        unsigned long long v = std::strtoull(cl.c_str(), &end, 10);
        if (end == cl.c_str() || (end && *end != '\0')) return -1;
        contentLength = static_cast<size_t>(v);
        if (contentLength > 10u * 1024 * 1024) return -1; /* sanity cap: 10 MiB */
    }

    if (rest.size() < contentLength) return 0; /* need more body bytes */

    req.body = rest.substr(0, contentLength);
    consumedUpTo = headerEnd + sepLen + contentLength;
    return 1;
}

/* ---------------- Response ---------------- */

const char* Response::statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default:  return "OK";
    }
}

void Response::setJson(int code, const std::string& jsonBody) {
    status = code;
    contentType = "application/json; charset=utf-8";
    body = jsonBody;
}

void Response::setHtml(int code, const std::string& htmlBody) {
    status = code;
    contentType = "text/html; charset=utf-8";
    body = htmlBody;
}

std::string Response::serialize() const {
    std::string out;
    out.reserve(body.size() + 256);
    out += "HTTP/1.1 ";
    out += std::to_string(status);
    out += ' ';
    out += statusText(status);
    out += "\r\nContent-Type: ";
    out += contentType;
    out += "\r\nContent-Length: ";
    out += std::to_string(body.size());
    out += "\r\nAccess-Control-Allow-Origin: *\r\n";
    out += closeAfter ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
    out += "Server: moms-magic-cpp/1.0\r\n\r\n";
    out += body;
    return out;
}

bool Response::writeAll(int fd, const char* data, size_t len) const {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false; /* EPIPE etc. -> SIGPIPE is ignored server-wide */
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

bool Response::send(int fd) const {
    std::string wire = serialize();
    return writeAll(fd, wire.data(), wire.size());
}

} // namespace http
