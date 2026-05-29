#!/usr/bin/env python3

from email.message import EmailMessage
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import smtplib
import ssl

PORT = int(os.environ.get("PORT", "8093"))
SHARED_KEY = os.environ["SHARED_KEY"]

SMTP_HOST = os.environ["SMTP_HOST"]
SMTP_PORT = int(os.environ.get("SMTP_PORT", "587"))
SMTP_USER = os.environ.get("SMTP_USER", "")
SMTP_PASS = os.environ.get("SMTP_PASS", "")
SMTP_STARTTLS = os.environ.get("SMTP_STARTTLS", "true").lower() in ("1", "true", "yes", "on")

MAIL_FROM = os.environ["MAIL_FROM"]
MAIL_TO = os.environ["MAIL_TO"]

SUBJECT_PREFIX = os.environ.get("SUBJECT_PREFIX", "[Bouncy] ")
MAX_BODY_BYTES = int(os.environ.get("MAX_BODY_BYTES", "4096"))
MAX_SUBJECT_CHARS = int(os.environ.get("MAX_SUBJECT_CHARS", "120"))
MAX_MESSAGE_CHARS = int(os.environ.get("MAX_MESSAGE_CHARS", "2000"))


class Handler(BaseHTTPRequestHandler):
    server_version = "email-relay/1.0"

    def _check_key(self):
        return self.headers.get("X-Email-Relay-Key") == SHARED_KEY

    def _send_json(self, status, obj):
        body = json.dumps(obj, sort_keys=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/send":
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

        subject = str(obj.get("subject", "")).strip()
        message = str(obj.get("message", "")).strip()

        if not subject:
            self._send_json(400, {"error": "subject is required"})
            return

        if not message:
            self._send_json(400, {"error": "message is required"})
            return

        if len(subject) > MAX_SUBJECT_CHARS:
            self._send_json(400, {"error": "subject too long"})
            return

        if len(message) > MAX_MESSAGE_CHARS:
            self._send_json(400, {"error": "message too long"})
            return

        full_subject = SUBJECT_PREFIX + subject

        try:
            send_email(full_subject, message)
        except Exception as e:
            print(f"send failed: {e}", flush=True)
            self._send_json(502, {"error": "email send failed"})
            return

        self._send_json(200, {"status": "sent"})

    def log_message(self, format, *args):
        return


def send_email(subject, message):
    msg = EmailMessage()
    msg["From"] = MAIL_FROM
    msg["To"] = MAIL_TO
    msg["Subject"] = subject
    msg.set_content(message)

    if SMTP_PORT == 465:
        context = ssl.create_default_context()
        with smtplib.SMTP_SSL(SMTP_HOST, SMTP_PORT, context=context, timeout=20) as smtp:
            if SMTP_USER:
                smtp.login(SMTP_USER, SMTP_PASS)
            smtp.send_message(msg)
    else:
        with smtplib.SMTP(SMTP_HOST, SMTP_PORT, timeout=20) as smtp:
            smtp.ehlo()
            if SMTP_STARTTLS:
                context = ssl.create_default_context()
                smtp.starttls(context=context)
                smtp.ehlo()
            if SMTP_USER:
                smtp.login(SMTP_USER, SMTP_PASS)
            smtp.send_message(msg)


if __name__ == "__main__":
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Listening on port {PORT}, sending mail to {MAIL_TO}", flush=True)
    server.serve_forever()
