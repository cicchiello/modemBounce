#!/usr/bin/env python3

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import tempfile

DATA_FILE = os.environ.get("DATA_FILE", "/data/wifi.json")
SHARED_KEY = os.environ["SHARED_KEY"]
MAX_BODY_BYTES = int(os.environ.get("MAX_BODY_BYTES", "8192"))

REQUIRED_FIELDS = {
    "version",
    "alg",
    "kdf",
    "salt",
    "nonce",
    "ciphertext",
    "tag",
}


class Handler(BaseHTTPRequestHandler):
    server_version = "wifi-store/1.0"

    def _check_key(self):
        return self.headers.get("X-Wifi-Store-Key") == SHARED_KEY

    def _send_json(self, status, obj):
        body = json.dumps(obj, sort_keys=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path != "/wifi":
            self.send_error(404)
            return

        if not self._check_key():
            self.send_error(401)
            return

        if not os.path.exists(DATA_FILE):
            self._send_json(404, {"error": "no wifi data stored"})
            return

        with open(DATA_FILE, "rb") as f:
            body = f.read()

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_PUT(self):
        if self.path != "/wifi":
            self.send_error(404)
            return

        if not self._check_key():
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

        raw = self.rfile.read(length)

        try:
            obj = json.loads(raw.decode("utf-8"))
        except Exception:
            self.send_error(400, "Invalid JSON")
            return

        missing = sorted(REQUIRED_FIELDS - set(obj.keys()))
        if missing:
            self._send_json(400, {"error": "missing required fields", "missing": missing})
            return

        # Store normalized JSON.  The encrypted fields remain opaque to this service.
        normalized = json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("utf-8") + b"\n"

        os.makedirs(os.path.dirname(DATA_FILE), exist_ok=True)

        fd, tmp_path = tempfile.mkstemp(
            prefix=".wifi.",
            suffix=".tmp",
            dir=os.path.dirname(DATA_FILE),
        )

        try:
            with os.fdopen(fd, "wb") as f:
                f.write(normalized)
                f.flush()
                os.fsync(f.fileno())

            os.replace(tmp_path, DATA_FILE)
        finally:
            if os.path.exists(tmp_path):
                os.unlink(tmp_path)

        self.send_response(204)
        self.end_headers()

    def log_message(self, format, *args):
        return


if __name__ == "__main__":
    port = int(os.environ.get("PORT", "8092"))
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"Listening on port {port}, storing data in {DATA_FILE}", flush=True)
    server.serve_forever()
