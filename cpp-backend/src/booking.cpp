/*
 * booking.cpp - Booking storage on a flat POSIX file.
 *
 * File handling design:
 *   <dataDir>/bookings.txt  - one line per booking:
 *       id|name|email|datetime|people|message|createdAt
 *     '|' and newlines inside fields are backslash-escaped, so a booking is
 *     always exactly one line.
 *   Writes are whole-file rewrites guarded by a std::mutex, so concurrent
 *   worker threads never interleave.  fsync() before close keeps the file
 *   consistent across crashes.
 */
#include "booking.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#include "json.hpp"
#include "util.hpp"

namespace app {

/* ---------------- Line (de)serialization ---------------- */

static std::string escapeField(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '|':  out += "\\p";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;
        }
    }
    return out;
}

static std::string unescapeField(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            switch (c) {
                case 'p':  out += '|';  break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case '\\': out += '\\'; break;
                default:   out += c;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

static std::string encodeBooking(const Booking& b) {
    std::string line;
    line += escapeField(b.id);
    line += '|';
    line += escapeField(b.name);
    line += '|';
    line += escapeField(b.email);
    line += '|';
    line += escapeField(b.datetime);
    line += '|';
    line += std::to_string(b.people);
    line += '|';
    line += escapeField(b.message);
    line += '|';
    line += std::to_string(static_cast<long long>(b.createdAt));
    line += '\n';
    return line;
}

static bool decodeBooking(const std::string& line, Booking& b) {
    std::vector<std::string> fields;
    std::string cur;
    bool esc = false;
    for (char c : line) {
        if (esc) { cur += c; esc = false; continue; }
        if (c == '\\') { cur += c; esc = true; continue; }
        if (c == '|') { fields.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    if (esc) cur.pop_back(); /* dangling backslash */
    fields.push_back(cur);

    if (fields.size() != 7) return false;
    b.id        = unescapeField(fields[0]);
    b.name      = unescapeField(fields[1]);
    b.email     = unescapeField(fields[2]);
    b.datetime  = unescapeField(fields[3]);
    b.people    = std::atoi(fields[4].c_str());
    b.message   = unescapeField(fields[5]);
    b.createdAt = static_cast<time_t>(std::atoll(fields[6].c_str()));
    b.time      = util::parseDateTime(b.datetime);
    return !b.id.empty();
}

/* ---------------- Booking ---------------- */

std::string Booking::toJson() const {
    json::Value v = json::Value::object();
    v.set("_id", id);          /* admin.html reads b._id */
    v.set("id", id);
    v.set("name", name);
    v.set("email", email);
    v.set("datetime", datetime);
    v.set("people", people);
    v.set("message", message);
    v.set("createdAt", static_cast<long long>(createdAt) * 1000); /* ms, like Mongo */
    return v.dump();
}

/* ---------------- BookingStore ---------------- */

BookingStore::BookingStore(std::string dataDir) : dataDir_(std::move(dataDir)) {}

bool BookingStore::load() {
    /* mkdir data dir if needed (mode 0755). */
    if (::mkdir(dataDir_.c_str(), 0755) < 0 && errno != EEXIST) {
        std::perror("mkdir data dir");
        return false;
    }

    std::string path = util::joinPath(dataDir_, "bookings.txt");
    std::string raw;
    if (!util::readFile(path, raw)) {
        /* Missing file is fine: start empty and create it. */
        return util::writeFile(path, "");
    }

    std::vector<Booking> loaded;
    for (const std::string& line : util::split(raw, '\n')) {
        if (line.empty()) continue;
        Booking b;
        if (decodeBooking(line, b)) loaded.push_back(b);
    }

    std::sort(loaded.begin(), loaded.end(), [](const Booking& a, const Booking& b) {
        if (a.createdAt != b.createdAt) return a.createdAt > b.createdAt;
        return a.id > b.id;
    });

    std::lock_guard<std::mutex> lock(mutex_); /* SIGHUP reload vs workers */
    bookings_ = std::move(loaded);
    return true;
}

bool BookingStore::tryCreate(const std::string& name,
                             const std::string& email,
                             const std::string& datetime,
                             int people,
                             const std::string& message,
                             Booking& out,
                             int& bookedInSlot,
                             int& seatsLeft) {
    if (name.empty() || email.empty() || datetime.empty() || people <= 0) {
        bookedInSlot = -1;
        return false;
    }

    time_t when = util::parseDateTime(datetime);
    if (when < 0) {
        bookedInSlot = -1;
        return false;
    }

    /* One-hour slot: [hour:00, hour+1:00). */
    std::tm tmSlot{};
    gmtime_r(&when, &tmSlot);
    tmSlot.tm_min = 0;
    tmSlot.tm_sec = 0;
    time_t slotStart = timegm(&tmSlot);
    time_t slotEnd = slotStart + 3600;

    std::lock_guard<std::mutex> lock(mutex_);

    int total = 0;
    for (const Booking& b : bookings_) {
        if (b.time >= slotStart && b.time < slotEnd) total += b.people;
    }

    const int kMaxCapacity = 10;
    bookedInSlot = total;
    seatsLeft = kMaxCapacity - total;
    if (total + people > kMaxCapacity) return false;

    Booking b;
    b.id        = generateId();
    b.name      = name;
    b.email     = email;
    b.datetime  = datetime;
    b.time      = when;
    b.people    = people;
    b.message   = message;
    b.createdAt = ::time(nullptr);

    /* Newest first. */
    bookings_.insert(
        std::upper_bound(bookings_.begin(), bookings_.end(), b,
                         [](const Booking& a, const Booking& c) {
                             if (a.createdAt != c.createdAt) return a.createdAt > c.createdAt;
                             return a.id > c.id;
                         }),
        b);

    if (!persistLocked()) return false;

    out = b;
    return true;
}

std::vector<Booking> BookingStore::listAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bookings_; /* already sorted newest first */
}

bool BookingStore::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(bookings_.begin(), bookings_.end(),
                           [&](const Booking& b) { return b.id == id; });
    if (it == bookings_.end()) return false;
    bookings_.erase(it);
    return persistLocked();
}

bool BookingStore::find(const std::string& id, Booking* out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Booking& b : bookings_) {
        if (b.id == id) {
            if (out) *out = b;
            return true;
        }
    }
    return false;
}

size_t BookingStore::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bookings_.size();
}

bool BookingStore::persistLocked() const {
    std::string raw;
    raw.reserve(bookings_.size() * 96);
    for (const Booking& b : bookings_) raw += encodeBooking(b);
    return util::writeFile(util::joinPath(dataDir_, "bookings.txt"), raw);
}

std::string BookingStore::generateId() {
    /* 24 hex chars like Mongo ObjectId: 8 chars of time + 16 random.
     * getrandom(2) with /dev/urandom fallback. */
    unsigned char buf[12];
    bool ok = false;

#if defined(__linux__)
    if (syscall(SYS_getrandom, buf, sizeof(buf), 0) == static_cast<ssize_t>(sizeof(buf))) {
        ok = true;
    }
#endif
    if (!ok) {
        int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            ok = ::read(fd, buf, sizeof(buf)) == static_cast<ssize_t>(sizeof(buf));
            ::close(fd);
        }
    }
    if (!ok) {
        /* Last resort: time + address entropy. */
        unsigned long long seed = static_cast<unsigned long long>(::time(nullptr)) << 17;
        seed ^= reinterpret_cast<unsigned long long>(buf);
        seed ^= static_cast<unsigned long long>(::clock());
        for (size_t i = 0; i < sizeof(buf); ++i) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            buf[i] = static_cast<unsigned char>(seed >> 33);
        }
    }

    static const char* hex = "0123456789abcdef";
    std::string id;
    id.reserve(24);
    for (unsigned char c : buf) {
        id += hex[c >> 4];
        id += hex[c & 0x0F];
    }
    return id;
}

} // namespace app
