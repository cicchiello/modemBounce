#!/usr/bin/env python3

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from datetime import datetime, timezone
import os

LOG_FILE = os.environ.get("LOG_FILE", "/data/bouncy.log")
SHARED_KEY = os.environ["SHARED_KEY"]
MAX_BODY_BYTES = int(os.environ.get("MAX_BODY_BYTES", "4096"))


class Handler(BaseHTTPRequestHandler):
    server_version = "bouncy-logger/1.0"

    def do_PUT(self):
        if self.path != "/log":
            self.send_error(404)
            return

        key = self.headers.get("X-Log-Key")
        if key != SHARED_KEY:
            self.send_error(401)
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error(400)
            return

        if length < 1:
            self.send_error(400, "Empty body")
            return

        if length > MAX_BODY_BYTES:
            self.send_error(413, "Body too large")
            return

        body = self.rfile.read(length)
        text = body.decode("utf-8", errors="replace").strip()

        timestamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
        line = f"{timestamp} {self.client_address[0]} {text}\n"

        os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)

        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(line)
            f.flush()

        self.send_response(204)
        self.end_headers()

    def log_message(self, format, *args):
        return


if __name__ == "__main__":
    port = int(os.environ.get("PORT", "8091"))
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"Listening on port {port}, logging to {LOG_FILE}", flush=True)
    server.serve_forever()
