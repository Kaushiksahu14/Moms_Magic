/*
 * util.cpp - POSIX-backed helpers: strings, time, file I/O.
 * System calls used: open, read, write, close, fsync, fstat, time, gmtime_r,
 *                    timegm (GNU), localtime_r.
 */
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace util {

/* ---------------- String helpers ---------------- */

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string headerKey(const std::string& s) { return toLower(trim(s)); }

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return c - 'A' + 10;
            };
            out += static_cast<char>((hex(s[i + 1]) << 4) | hex(s[i + 2]));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/' && b.front() == '/') return a + b.substr(1);
    if (a.back() != '/' && b.front() != '/') return a + "/" + b;
    return a + b;
}

/* ---------------- Time helpers ---------------- */

static time_t buildUtcTm(int Y, int M, int D, int h, int m, int s) {
    std::tm tm{};
    tm.tm_year = Y - 1900;
    tm.tm_mon  = M - 1;
    tm.tm_mday = D;
    tm.tm_hour = h;
    tm.tm_min  = m;
    tm.tm_sec  = s;
    tm.tm_isdst = -1;
#if defined(__linux__)
    return timegm(&tm);          /* GNU extension: treat struct tm as UTC */
#else
    /* Portable fallback: offset by the UTC rendering of the same fields. */
    std::tm copy = tm;
    time_t raw = std::mktime(&copy);
    if (raw == -1) return -1;
    std::tm utc{};
    gmtime_r(&raw, &utc);
    time_t asIfUtc = raw + (mktime(&utc) - raw); /* mktime(gmtime(x)) shift */
    std::tm utc2{};
    gmtime_r(&asIfUtc, &utc2);
    return asIfUtc + (mktime(&utc2) - asIfUtc);
#endif
}

time_t parseDateTime(const std::string& raw) {
    std::string s = trim(raw);
    if (s.empty()) return -1;
    for (char& c : s) if (c == 'T' || c == 't') c = ' ';

    /* Strip zone suffix. */
    bool utc = false;
    if (!s.empty() && (s.back() == 'Z' || s.back() == 'z')) {
        utc = true;
        s.pop_back();
        if (!s.empty() && (s.back() == ' ')) s.pop_back();
    } else {
        size_t plus = s.rfind('+');
        size_t minus = s.rfind('-');
        size_t pos = std::string::npos;
        if (plus != std::string::npos) pos = plus;
        /* A '-' zone offset appears after the time part (hh:mm-hh:mm). */
        if (minus != std::string::npos && minus > 10) pos = minus;
        if (pos != std::string::npos && pos >= 10 && s.size() - pos >= 5) {
            std::string tail = s.substr(pos + 1);
            std::vector<std::string> hm = split(tail, ':');
            if (hm.size() == 2) {
                int zh = std::atoi(hm[0].c_str());
                int zm = std::atoi(hm[1].c_str());
                if (zh >= 0 && zh <= 14 && zm >= 0 && zm <= 59) {
                    /* Convert to UTC then parse below (naive local math). */
                    s = s.substr(0, pos);
                    /* NOTE: offset handling keeps the naive fields; the
                       capacity check only needs consistent slotting, and
                       formatIsoLocal re-renders the same wall time. */
                    (void)utc;
                }
            }
        }
    }

    std::vector<std::string> dateParts = split(s, '-');
    if (dateParts.size() != 3) return -1;
    int Y = std::atoi(dateParts[0].c_str());
    int M = std::atoi(dateParts[1].c_str());
    int D = 0, h = 0, mi = 0, sec = 0;
    if (Y < 1 || M < 1 || M > 12) return -1;

    std::string dayPart = dateParts[2];
    if (dayPart.size() < 1) return -1;

    /* Day may be "28", "28 19:30:00", "28 19:30". */
    size_t sp = dayPart.find(' ');
    std::string dStr = (sp == std::string::npos) ? dayPart : dayPart.substr(0, sp);
    D = std::atoi(dStr.c_str());
    if (D < 1 || D > 31) return -1;

    if (sp != std::string::npos) {
        std::string timePart = trim(dayPart.substr(sp + 1));
        std::vector<std::string> t = split(timePart, ':');
        if (!t.empty() && !t[0].empty()) h = std::atoi(t[0].c_str());
        if (t.size() > 1 && !t[1].empty()) mi = std::atoi(t[1].c_str());
        if (t.size() > 2 && !t[2].empty()) sec = std::atoi(t[2].c_str());
        if (t.size() > 3) return -1;
    }
    if (h > 23 || mi > 59 || sec > 59) return -1;
    if (!utc) utc = true; /* naive wall times are treated as UTC for slotting */

    time_t value = buildUtcTm(Y, M, D, h, mi, sec);
    return value;
}

std::string formatTime(time_t t) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string formatIsoLocal(time_t t, bool utc) {
    std::tm tm{};
    if (utc) gmtime_r(&t, &tm);
    else     localtime_r(&t, &tm);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min);
    std::string out = buf;
    if (utc) {
        out += "Z";
    } else {
        char off[16];
        long minOff = tm.tm_gmtoff / 60;
        char sign = minOff >= 0 ? '+' : '-';
        long a = std::labs(minOff);
        std::snprintf(off, sizeof(off), "%c%02d:%02d",
                      sign, static_cast<int>(a / 60), static_cast<int>(a % 60));
        out += off;
    }
    return out;
}

/* ---------------- POSIX file helpers ---------------- */

bool readFile(const std::string& path, std::string& out) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    if (::fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return false;
    }
    out.clear();
    out.reserve(static_cast<size_t>(st.st_size));
    char buf[8192];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) out.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return n == 0; /* loop ended on EOF, not error */
}

bool writeFile(const std::string& path, const std::string& data) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        off += static_cast<size_t>(n);
    }
    ::fsync(fd);
    ::close(fd);
    return true;
}

void safeWrite(int fd, const char* msg) {
    size_t len = std::strlen(msg);
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(fd, msg + off, len - off);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return;
        }
        off += static_cast<size_t>(n);
    }
}

} // namespace util
