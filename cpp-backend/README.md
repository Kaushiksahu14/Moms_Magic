# MOM'S MAGIC — C++ Backend (Linux / POSIX)

The same restaurant-booking backend that was built with Node/Express/MongoDB,
re-implemented as a **C++17 Linux systems application**. The frontend
(`index.html`, `booking.html`, `admin.html`, ...) is served by the same C++
process — no Node.js, no database server.

| Technology | Where it is used |
|---|---|
| **C++** | All of `src/` — OOP (`IRouteHandler` hierarchy, `BookingStore`), RAII |
| **Linux** | Build & runtime target (gcc/Makefile, bash) |
| **System Programming** | Processes (`fork`/`setsid` daemon, PID file), threads (worker pool), signals (`SIGINT`/`SIGTERM`/`SIGHUP`/`SIGPIPE`, self-pipe), system calls (`socket`, `bind`, `listen`, `accept4`, `poll`, `read`, `write`, `fsync`, `open`, `close`, `mkdir`, `nanosleep`, `getrandom`) |
| **File Handling** | `data/bookings.txt` flat file store (one line per booking, escaped fields, atomic rewrite + `fsync`) |
| **POSIX APIs** | Sockets, `sigaction`, `poll`, `/dev/urandom`, `timegm`/`gmtime_r` |
| **STL** | `std::vector`, `std::map`, `std::string`, `std::thread`, `<algorithm>` |

## Build & run (Linux)

```bash
cd cpp-backend
make            # -> bin/server
./bin/server    # http://localhost:5000
```

Then open `http://localhost:5000/booking.html` and
`http://localhost:5000/admin.html` — they talk to this server.

Options:

```bash
./bin/server --port 5000 --data ./data --root .. [--daemon]
```

* `--daemon` — double-fork daemonization (setsid, stdio → /dev/null)
* `SIGHUP`   — reload `bookings.txt` without restarting
* `SIGINT`/`SIGTERM` — graceful shutdown (finishes in-flight responses)

## API (drop-in compatible with the Node backend)

| Method | Path | Description |
|---|---|---|
| POST | `/api/book` | Create a booking `{name, email, datetime, people, message}`; **max 10 people per 1-hour slot** → `400` when full |
| GET | `/api/bookings` | All bookings, newest first (JSON array with `_id`) |
| DELETE | `/api/bookings/:id` | Delete a booking |
| GET | `/api/status` | Server stats (pid, workers, request counters) |

Any other path is served as a static file from the project root
(traversal-protected), so the whole website works from this one process.

## Tests

```bash
make test       # end-to-end smoke test (needs curl)
make debug      # ASan/UBSan build
```

`test/smoke.sh` verifies: 10 successful bookings, the 11th rejected by the
capacity rule, listing, deletion, static files, API 404 and path traversal
protection.

## Layout

```
cpp-backend/
├── Makefile
├── src/
│   ├── main.cpp            # args, daemon, signals, wiring
│   ├── server.{hpp,cpp}    # POSIX socket server, thread pool, poll loop
│   ├── http.{hpp,cpp}      # request parsing / response building
│   ├── router.{hpp,cpp}    # OOP route handlers + static files
│   ├── booking.{hpp,cpp}   # model + file-backed store (business rule)
│   ├── booking_handlers.*  # concrete handlers for the 3 API routes
│   ├── json.{hpp,cpp}      # minimal JSON parser/serializer
│   └── util.{hpp,cpp}      # strings, time, POSIX file helpers
└── test/smoke.sh
```
