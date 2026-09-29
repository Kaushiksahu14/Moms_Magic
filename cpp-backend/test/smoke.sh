#!/usr/bin/env bash
#
# Smoke test: exercises the three API endpoints end-to-end.
# Usage: bash test/smoke.sh [port]     (default 5010)
#
set -u

PORT="${1:-5010}"
BASE="http://127.0.0.1:${PORT}"
DATA_DIR="$(mktemp -d)"
BIN="./bin/server"

stop_server() {
    kill "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null   # reap; graceful shutdown takes ~ms
}

fail() { echo "FAIL: $*" >&2; stop_server; rm -rf "$DATA_DIR"; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "curl is required"; }

need curl

echo "[smoke] starting server on port $PORT (data: $DATA_DIR)"
"$BIN" --port "$PORT" --data "$DATA_DIR" &
SERVER_PID=$!
trap 'stop_server' EXIT

for _ in $(seq 1 50); do
    curl -sf "$BASE/api/status" >/dev/null 2>&1 && break
    sleep 0.1
done

echo "[smoke] POST /api/book x10 (capacity should stop the 11th)"
ids=()
for i in $(seq 1 10); do
    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$BASE/api/book" \
        -H 'Content-Type: application/json' \
        -d "{\"name\":\"Guest $i\",\"email\":\"g$i@test.com\",\"datetime\":\"2027-01-01T19:00:00Z\",\"people\":1}")
    [ "$code" = "200" ] || fail "booking $i expected 200, got $code"
done

code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$BASE/api/book" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Overflow","email":"o@test.com","datetime":"2027-01-01T19:30:00Z","people":1}')
[ "$code" = "400" ] || fail "11th booking expected 400 (slot full), got $code"
echo "[smoke] capacity rule OK (max 10 per hour slot)"

echo "[smoke] GET /api/bookings"
body=$(curl -s "$BASE/api/bookings")
count=$(printf '%s' "$body" | grep -o '_id' | wc -l)
[ "$count" = "10" ] || fail "expected 10 bookings, got $count"

id=$(printf '%s' "$body" | grep -o '"_id":"[0-9a-f]\{24\}"' | head -1 | cut -d'"' -f4)
[ -n "$id" ] || fail "could not extract booking id"

echo "[smoke] DELETE /api/bookings/$id"
code=$(curl -s -o /dev/null -w '%{http_code}' -X DELETE "$BASE/api/bookings/$id")
[ "$code" = "200" ] || fail "delete expected 200, got $code"

count=$(curl -s "$BASE/api/bookings" | grep -o '_id' | wc -l)
[ "$count" = "9" ] || fail "expected 9 bookings after delete, got $count"

echo "[smoke] SIGHUP keeps serving"
count_before=$(curl -s "$BASE/api/bookings" | grep -o '_id' | wc -l)
kill -HUP "$SERVER_PID"
sleep 0.5
count_after=$(curl -s --max-time 5 "$BASE/api/bookings" | grep -o '_id' | wc -l)
[ "$count_after" = "$count_before" ] || fail "SIGHUP broke serving ($count_before -> $count_after)"

echo "[smoke] static file + 404"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/admin.html")
[ "$code" = "200" ] || fail "admin.html expected 200, got $code"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/nope")
[ "$code" = "404" ] || fail "unknown api expected 404, got $code"
code=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is "$BASE/../../etc/passwd")
[ "$code" = "403" ] || [ "$code" = "404" ] || fail "traversal expected 403/404, got $code"

stop_server
rm -rf "$DATA_DIR"
echo "[smoke] ALL TESTS PASSED"
