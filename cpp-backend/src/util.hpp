/*
 * util.hpp - Small helpers: strings, time, POSIX file I/O.
 * MOM'S MAGIC - C++ / Linux system programming backend.
 */
#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <ctime>

namespace util {

/* ---------------- String helpers ---------------- */

std::string trim(const std::string& s);
std::string toLower(const std::string& s);

/* Case-insensitive header lookup key. */
std::string headerKey(const std::string& s);

/* Percent-decoding (%20, %2F, ...) for URL components. */
std::string urlDecode(const std::string& s);

/* UTF-8 JSON string escaping (" -> \", newline -> \n, ...). */
std::string jsonEscape(const std::string& s);

/* Split "a, b, c" on sep into trimmed parts. */
std::vector<std::string> split(const std::string& s, char sep);

/* Join path a/b keeping exactly one slash. */
std::string joinPath(const std::string& a, const std::string& b);

/* ---------------- Time helpers ---------------- */

/*
 * Parse ISO-8601-ish date-times produced by JS Date.toISOString(),
 * HTML datetime-local inputs and human input, e.g.:
 *   2026-09-28T19:30:00.000Z | 2026-09-28T19:30 | 2026-09-28 19:30
 * Returns -1 on failure.
 */
time_t parseDateTime(const std::string& s);

/* UTC "YYYY-MM-DD HH:MM:SS" rendering (logging / admin display). */
std::string formatTime(time_t t);

/*
 * "YYYY-MM-DDTHH:MM" (datetime-local style). Zone suffix is appended:
 * "Z" for UTC or "+05:30" style offsets for local time.
 */
std::string formatIsoLocal(time_t t, bool utc);

/* ---------------- POSIX file helpers ---------------- */

/* Whole file into memory; false if missing/unreadable. */
bool readFile(const std::string& path, std::string& out);

/* Binary-safe write: open, write all, fsync, close. */
bool writeFile(const std::string& path, const std::string& data);

/* Signal-safe write of a tiny message (async-signal-safe usage). */
void safeWrite(int fd, const char* msg);

} // namespace util
