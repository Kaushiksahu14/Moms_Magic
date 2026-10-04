# MOM'S MAGIC

A restaurant website with an online **table-booking system** and a password-protected **admin dashboard**.

The same REST API is implemented twice, so you can pick whichever stack suits you:

| | Backend A: Node.js | Backend B: C++17 (Linux) |
|---|---|---|
| Folder | `backend/` | `cpp-backend/` |
| Stack | Express 5, MongoDB, Mongoose | POSIX sockets, thread pool, flat-file storage |
| Database server needed | Yes (MongoDB) | No |
| Serves the website too | Yes | Yes |

Both listen on **port 5000** by default and expose identical endpoints, so the frontend works unchanged with either one.

---

## Features

- **Public website**: Home, About, Service, Menu, Booking, Team, Testimonial and Contact pages (Bootstrap 5).
- **Table booking** (`booking.html`): guests book a date and time. Each **1-hour slot holds at most 12 people**; extra requests are refused with a clear message.
- **Admin dashboard** (`admin.html`):
  - Manager login with a signed token (valid 8 hours, expired sessions return to the login screen)
  - Summary cards: total, upcoming, today, guests expected
  - Search, upcoming/past filter, sortable date column
  - Delete bookings with confirmation, auto-refresh every minute
  - Guest input is rendered as text, never as HTML, so it cannot inject scripts
- **Protected API**: listing and deleting bookings require the admin token. Creating a booking stays public.

---

## Quick start

### Option A: C++ backend (no database needed)

Requires Linux, `g++` (C++17) and `make`.

```bash
cd cpp-backend
make
./bin/server
```

### Option B: Node.js backend

Requires Node.js 18+ and a running MongoDB.

```bash
cd backend
npm install
```

Create `backend/.env`:

```env
MONGO_URI=mongodb://127.0.0.1:27017/moms-magic
PORT=5000

# Admin dashboard login: change these!
ADMIN_USERNAME=manager
ADMIN_PASSWORD=change-me
ADMIN_TOKEN_SECRET=a-long-random-string
```

Then start it:

```bash
node server.js
```

### Open the site

| Page | URL |
|---|---|
| Website | http://localhost:5000/ |
| Booking | http://localhost:5000/booking.html |
| Admin | http://localhost:5000/admin.html |

> The default admin account is `manager` / `magic123`. **Change it before sharing or deploying.**

---

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `ADMIN_USERNAME` | `manager` | Admin login name |
| `ADMIN_PASSWORD` | `magic123` | Admin password |
| `ADMIN_TOKEN_SECRET` | random per start (C++) / dev value (Node) | Key used to sign login tokens. Set it so sessions survive a restart |
| `MONGO_URI` | none | MongoDB connection string (Node only) |
| `PORT` | `5000` | HTTP port (Node only; C++ uses `--port`) |

C++ server options:

```bash
./bin/server --port 5000 --data ./data --root .. [--daemon]
```

| Option | Meaning |
|---|---|
| `--port N` | TCP port (default 5000) |
| `--data DIR` | Where `bookings.txt` is stored (default `./data`) |
| `--root DIR` | Website folder to serve (default `..`) |
| `--daemon` | Run in the background (double-fork) |

The C++ server also reloads `bookings.txt` on `SIGHUP` and shuts down gracefully on `SIGINT`/`SIGTERM`.

---

## API

| Method | Path | Auth | Description |
|---|---|---|---|
| POST | `/api/book` | none | Create a booking `{name, email, datetime, people, message}`. Returns `400` if the slot is full |
| GET | `/api/bookings` | admin | List all bookings, newest first |
| DELETE | `/api/bookings/:id` | admin | Delete a booking |
| POST | `/api/admin/login` | none | `{username, password}` → `{token}` |
| GET | `/api/admin/protected` | admin | Returns `200` if the token is valid (C++ backend) |
| GET | `/api/status` | none | Health check |

Admin requests send the token as a header: `Authorization: Bearer <token>`.

Example:

```bash
TOKEN=$(curl -s -X POST http://localhost:5000/api/admin/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"manager","password":"magic123"}' | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')

curl http://localhost:5000/api/bookings -H "Authorization: Bearer $TOKEN"
```

---

## Testing

The C++ backend has an end-to-end smoke test (needs `curl`):

```bash
cd cpp-backend
make test
```

It checks admin login and token rejection, 10 successful bookings, the 11th refused by the capacity rule, listing, deletion, `SIGHUP` reload, static files, API 404s and path-traversal protection.

`make debug` builds with AddressSanitizer and UBSan.

---

## Project structure

```
MOM'S MAGIC/
├── index.html, about.html, service.html, menu.html,
│   booking.html, team.html, testimonial.html, contact.html
├── admin.html              # Admin login + bookings dashboard
├── css/, js/, lib/, img/   # Styles, scripts, vendor libraries, images
├── scss/                   # Bootstrap SCSS source
├── backend/                # Node.js / Express / MongoDB backend
│   ├── server.js
│   ├── routes/             # bookingRoutes.js, adminRoutes.js
│   ├── middleware/auth.js  # Signed tokens + requireAdmin
│   └── models/Booking.js
├── cpp-backend/            # C++17 Linux backend (see its README)
│   ├── src/                # server, http, router, booking, auth, sha256, json, util
│   ├── test/smoke.sh
│   └── Makefile
├── DOCUMENTATION.md / .pdf # Full project report
└── README.md
```

---

## How booking works

1. The guest submits the form on `booking.html`, which calls `POST /api/book`.
2. The server rounds the requested time down to the hour and adds up the people already booked in that hour.
3. If the total plus the new party is more than **10**, it answers `400` ("This time slot is full"). Otherwise the booking is saved.
4. The manager signs in on `admin.html`, which loads the list with the admin token and can delete entries.

---

## Technologies

- **Frontend:** HTML5, CSS3, Bootstrap 5, vanilla JavaScript (`fetch`), jQuery, Owl Carousel, WOW.js, Tempus Dominus
- **Node backend:** Node.js, Express 5, MongoDB, Mongoose, dotenv, cors
- **C++ backend:** C++17, POSIX sockets, `poll`, thread pool, `fork`/`setsid` daemon mode, signals, flat-file storage with `fsync`, HMAC-SHA256 tokens (own SHA-256 implementation), Make

For the full write-up (architecture, data model, security notes, demo script) see [`DOCUMENTATION.md`](DOCUMENTATION.md). For C++ internals see [`cpp-backend/README.md`](cpp-backend/README.md).

---

## Security notes

- Change the default admin credentials and set `ADMIN_TOKEN_SECRET` before any real use.
- Login tokens are HMAC-signed and expire after 8 hours.
- Credential and token comparisons are constant-time.
- Static file serving blocks `..` traversal and hidden files; the Node server also hides `backend/`, `cpp-backend/` and `.env`.
- The admin token is kept in the browser's `localStorage`; log out on shared computers.
- There is no HTTPS or rate limiting built in. Put the app behind a reverse proxy (e.g. nginx) for anything public.
