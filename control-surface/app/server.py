#!/usr/bin/env python3
"""Control surface for the aquarium light.

Serves a page, proxies the device API, and records state history the device
itself cannot keep - it has no storage and its RAM is spoken for.

The device credentials live here rather than in the browser: the page is
same-origin, and this process injects basic auth on the way through.

Usage:  python3 server.py          (configured entirely by environment)
Assumes: the light answers on LIGHT_HOST and speaks the /api/* contract in
         the aquarium-light repo. Standard library only - no pip, no build.
"""

import base64
import json
import os
import sqlite3
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

LIGHT_HOST = os.environ.get("LIGHT_HOST", "192.168.1.166")
LIGHT_USER = os.environ.get("LIGHT_USER", "aquarium")
LIGHT_PASS = os.environ.get("LIGHT_PASS", "")
PORT = int(os.environ.get("PORT", "8181"))
POLL_SECONDS = int(os.environ.get("POLL_SECONDS", "10"))
RETAIN_DAYS = int(os.environ.get("RETAIN_DAYS", "14"))
DB_PATH = os.environ.get("DB_PATH", "/data/history.db")
HERE = os.path.dirname(os.path.abspath(__file__))

AUTH_HEADER = "Basic " + base64.b64encode(
    f"{LIGHT_USER}:{LIGHT_PASS}".encode()
).decode()

SCHEMA = """
CREATE TABLE IF NOT EXISTS samples (
  ts INTEGER PRIMARY KEY,
  reachable INTEGER NOT NULL,
  mode INTEGER, scheduled INTEGER, override INTEGER,
  brightness INTEGER, fps INTEGER, rssi INTEGER, heap INTEGER,
  uptime INTEGER, clock_synced INTEGER, test_mode INTEGER, dropouts INTEGER,
  effects TEXT
);
CREATE INDEX IF NOT EXISTS samples_ts ON samples(ts);
"""

db_lock = threading.Lock()


def db():
    conn = sqlite3.connect(DB_PATH, timeout=10)
    conn.execute("PRAGMA journal_mode=WAL")
    return conn


MAX_BODY = 16 * 1024


def device(path, method="GET", timeout=6, body=None, ctype=None):
    """One call to the light. Returns (status, body) or raises."""
    req = urllib.request.Request(f"http://{LIGHT_HOST}{path}", method=method,
                                 data=body)
    if method != "GET":
        req.add_header("Authorization", AUTH_HEADER)
        if body is None:
            req.add_header("Content-Length", "0")
        elif ctype:
            req.add_header("Content-Type", ctype)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


def poll_once():
    row = {"reachable": 0}
    try:
        _, body = device("/api/status")
        s = json.loads(body)
        row = {
            "reachable": 1,
            "mode": s.get("mode"),
            "scheduled": s.get("scheduledMode"),
            "override": s.get("modeOverride"),
            "brightness": s.get("brightness"),
            "fps": s.get("fps"),
            "rssi": s.get("rssi"),
            "heap": s.get("heapFree"),
            "uptime": s.get("uptimeSec"),
            "clock_synced": 1 if s.get("clockSynced") else 0,
            "test_mode": 1 if s.get("testMode") else 0,
            "dropouts": s.get("dropouts"),
            "effects": json.dumps(
                {e["name"]: e["level"] for e in s.get("effects", [])}
            ),
        }
    except Exception:
        # An unreachable light is itself the fact worth recording.
        pass

    cols = ["reachable", "mode", "scheduled", "override", "brightness", "fps",
            "rssi", "heap", "uptime", "clock_synced", "test_mode", "dropouts",
            "effects"]
    with db_lock:
        conn = db()
        conn.execute(
            f"INSERT OR REPLACE INTO samples (ts,{','.join(cols)}) "
            f"VALUES ({','.join('?' * (len(cols) + 1))})",
            [int(time.time())] + [row.get(c) for c in cols],
        )
        conn.execute("DELETE FROM samples WHERE ts < ?",
                     (int(time.time()) - RETAIN_DAYS * 86400,))
        conn.commit()
        conn.close()


def poller():
    while True:
        try:
            poll_once()
        except Exception as exc:              # never let the thread die
            print(f"poll error: {exc}", flush=True)
        time.sleep(POLL_SECONDS)


def history(hours, cap=700):
    since = int(time.time()) - int(hours * 3600)
    with db_lock:
        conn = db()
        rows = conn.execute(
            "SELECT ts,reachable,mode,brightness,fps,rssi,effects "
            "FROM samples WHERE ts >= ? ORDER BY ts", (since,)
        ).fetchall()
        conn.close()

    # Downsample by taking every nth sample, but never drop the edge of an
    # outage. Every nth alone loses short ones entirely, and "was the light up
    # last night" is the question this history exists to answer - a missing gap
    # reads as uptime. Keeping both edges of each run bounds the cost: a long
    # outage still collapses to two rows. Mode can still lose a brief state;
    # /api/status is the authority on the present.
    step = max(1, len(rows) // cap)
    keep = []
    for i, r in enumerate(rows):
        edge = ((i > 0 and rows[i - 1][1] != r[1]) or
                (i + 1 < len(rows) and rows[i + 1][1] != r[1]))
        if i % step == 0 or edge:
            keep.append(r)

    return [
        {"ts": r[0], "up": r[1], "mode": r[2], "brightness": r[3],
         "fps": r[4], "rssi": r[5], "effects": json.loads(r[6]) if r[6] else {}}
        for r in keep
    ]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass                                   # the poller would swamp the log

    def _send(self, code, ctype, body):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path

        if path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                return self._send(200, "text/html; charset=utf-8", f.read())

        if path == "/api/history":
            q = urlparse(self.path).query
            hours = 24.0
            for part in q.split("&"):
                if part.startswith("hours="):
                    try:
                        hours = float(part[6:])
                    except ValueError:
                        pass
            return self._send(200, "application/json",
                              json.dumps(history(hours)))

        if path in ("/api/status", "/api/frame"):
            try:
                _, body = device(path)
                ctype = ("application/json" if path.endswith("status")
                         else "text/plain")
                return self._send(200, ctype, body)
            except Exception:
                # Say so in-band rather than 502ing: the page draws an
                # "unreachable" state, which is information, not an error.
                return self._send(200, "application/json",
                                  json.dumps({"reachable": False}))

        self._send(404, "text/plain", "no such endpoint")

    def do_POST(self):
        path = urlparse(self.path).path

        # Always read the body, even when rejecting: leaving it in the socket
        # desyncs a keep-alive connection.
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(min(length, MAX_BODY)) if length else None

        if not path.startswith("/api/"):
            return self._send(404, "text/plain", "no such endpoint")
        if length > MAX_BODY:
            return self._send(413, "application/json",
                              json.dumps({"ok": False, "error": "body too large"}))
        try:
            code, body = device(self.path, method="POST", body=body,
                                ctype=self.headers.get("Content-Type"))
            return self._send(code, "application/json", body)
        except urllib.error.HTTPError as exc:
            return self._send(exc.code, "application/json",
                              json.dumps({"ok": False, "error": exc.reason}))
        except Exception as exc:
            return self._send(504, "application/json",
                              json.dumps({"ok": False, "error": str(exc)}))


def main():
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    with db_lock:
        conn = db()
        conn.executescript(SCHEMA)
        conn.commit()
        conn.close()

    threading.Thread(target=poller, daemon=True).start()
    print(f"control surface on :{PORT}, light at {LIGHT_HOST}, "
          f"polling every {POLL_SECONDS}s", flush=True)
    ThreadingHTTPServer(("", PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
