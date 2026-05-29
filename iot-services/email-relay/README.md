# email-relay

`email-relay` is a small LAN-only HTTP service that lets the Feather M0 send monitoring emails without embedding SMTP credentials in the firmware.

The Feather sends a simple HTTP `POST` request to `pi-nas`; this service validates a shared key and sends the email through the configured SMTP account.

---

## Purpose

The Feather M0 is a small monitoring device. It should be able to report notable events by email, but it should **not** know the SMTP server password.

This service provides a local relay:

```text
Feather M0
  HTTP POST /send
  shared-key header
  JSON body with subject/message
        |
        v
pi-nas email-relay
  validates request
  sends email using SMTP credentials stored on pi-nas
        |
        v
configured email recipients
```

---

## Host environment

This service is intended to run on:

- Host: `pi-nas`
- Hardware: Raspberry Pi 5
- NAS manager: OpenMediaVault
- Container runtime: Docker / OMV Compose
- LAN IP: `10.0.0.214`
- Service port: `8093`

The service listens on:

```text
http://10.0.0.214:8093/send
```

Only HTTP `POST` to `/send` is supported.

---

## Repository layout

```text
~/workspace/modemBounce/iot-services/email-relay/
  Dockerfile
  docker-compose.yml
  email_relay.py
  .env              # local secrets; not committed
  .env.example      # documented example; safe to commit
  .gitignore
  README.md
```

Convenience symlink:

```text
/opt/pi-nas-services/email-relay -> /home/joe/workspace/modemBounce/iot-services/email-relay
```

The git repo is the preferred authoritative source for the service.

---

## API

### Endpoint

```http
POST /send
```

### Required header

```http
X-Email-Relay-Key: <shared-key>
```

The key is stored in `.env` as:

```text
SHARED_KEY=...
```

The Feather firmware should store the same value in `arduino_secrets.h`, for example:

```cpp
#define SECRET_EMAIL_RELAY_KEY "..."
```

### Request body

The request body is JSON:

```json
{
  "subject": "Feather alert",
  "message": "Monitoring event details go here."
}
```

The service intentionally does **not** accept a `to` field from the Feather. Recipients are fixed in `.env` so a compromised or buggy device cannot send arbitrary email to arbitrary recipients.

### Success response

```http
HTTP/1.0 200 OK
Content-Type: application/json
```

```json
{"status": "sent"}
```

### Common error responses

```text
401 Unauthorized       missing/wrong shared key
404 Not Found          path other than /send
400 Bad Request        invalid JSON or missing fields
413 Payload Too Large  request body too large
502 Bad Gateway        SMTP send failed
```

---

## Configuration

Runtime configuration is stored in `.env`.

The file is intentionally ignored by git.

Example:

```text
SHARED_KEY=replace-with-random-hex-key

SMTP_HOST=smtp.gmail.com
SMTP_PORT=587
SMTP_STARTTLS=true
SMTP_USER=your-smtp-username
SMTP_PASS=your-smtp-password-or-app-password

MAIL_FROM=pi-nas@example.com
MAIL_TO=you@example.com,alternate@example.com

SUBJECT_PREFIX=[Bouncy] 
```

### Settings

| Variable | Purpose |
|---|---|
| `SHARED_KEY` | HTTP shared key required from the Feather |
| `SMTP_HOST` | SMTP server hostname |
| `SMTP_PORT` | SMTP server port, usually `587` for STARTTLS or `465` for SMTP-over-SSL |
| `SMTP_STARTTLS` | `true` for STARTTLS on port 587 |
| `SMTP_USER` | SMTP username |
| `SMTP_PASS` | SMTP password or app password |
| `MAIL_FROM` | Sender address |
| `MAIL_TO` | Comma-separated recipient list |
| `SUBJECT_PREFIX` | Prefix added to every email subject |
| `MAX_BODY_BYTES` | Optional HTTP request size limit |
| `MAX_SUBJECT_CHARS` | Optional subject length limit |
| `MAX_MESSAGE_CHARS` | Optional message length limit |

---

## SMTP configuration source

This service was configured using the same SMTP account as OMV notifications.

OMV notification config can be inspected with:

```bash
sudo omv-confdbadm read conf.system.notification.email | python3 -m json.tool
```

OMV redacts the password in that output. The real SMTP password or Gmail app password still needs to be entered manually into `email-relay/.env`.

For the current setup, the non-secret values are based on OMV’s email notification settings:

```text
SMTP_HOST=smtp.gmail.com
SMTP_PORT=587
SMTP_STARTTLS=true
SMTP_USER=<same Gmail account used by OMV>
MAIL_FROM=<same sender used by OMV>
MAIL_TO=<configured monitoring recipients>
```

Do not paste or commit the SMTP password.

---

## Creating `.env`

Copy the example:

```bash
cp .env.example .env
chmod 600 .env
```

Generate a shared key:

```bash
openssl rand -hex 24
```

Edit `.env`:

```bash
nano .env
```

Keep `.env` out of git.

Check that `.env` is ignored:

```bash
git status --ignored
```

---

## Docker Compose

The service runs with Docker Compose.

Important settings in `docker-compose.yml`:

```yaml
ports:
  - "10.0.0.214:8093:8093"
```

This binds the service to the NAS LAN IP instead of all interfaces.

```yaml
restart: unless-stopped
```

This causes the container to restart after reboot unless it was explicitly stopped.

The service does not need a data volume. It only stores configuration in `.env` and sends emails through SMTP.

---

## Basic commands

Run commands from the service directory:

```bash
cd /home/joe/workspace/modemBounce/iot-services/email-relay
```

### Start or rebuild

```bash
sudo docker compose up -d --build
```

### Stop

```bash
sudo docker compose down
```

### Show container status

```bash
sudo docker ps --filter name=email-relay
```

Expected port binding:

```text
10.0.0.214:8093->8093/tcp
```

### View logs

```bash
sudo docker compose logs
```

Follow logs live:

```bash
sudo docker compose logs -f
```

### Check restart policy

```bash
sudo docker inspect -f '{{.HostConfig.RestartPolicy.Name}}' email-relay
```

Expected:

```text
unless-stopped
```

---

## Testing

### Unauthorized request test

This should fail with `401 Unauthorized`:

```bash
curl -i \
  -X POST \
  -H 'X-Email-Relay-Key: wrong-key' \
  -H 'Content-Type: application/json' \
  --data '{"subject":"test","message":"this should be rejected"}' \
  http://10.0.0.214:8093/send
```

Expected:

```text
HTTP/1.0 401 Unauthorized
```

### Authorized send test

This reads the shared key from `.env` without printing it:

```bash
curl -i \
  -X POST \
  -H "X-Email-Relay-Key: $(sed -n 's/^SHARED_KEY=//p' .env)" \
  -H 'Content-Type: application/json' \
  --data '{"subject":"email-relay test","message":"This is a test message from the pi-nas email-relay service."}' \
  http://10.0.0.214:8093/send
```

Expected:

```text
HTTP/1.0 200 OK
```

```json
{"status": "sent"}
```

Then confirm the email arrives at the configured recipients.

### If the send fails

Check the service logs:

```bash
sudo docker compose logs --tail=50
```

A `502` response means the HTTP request was accepted but the SMTP send failed.

---

## Feather client shape

The Feather calls:

```text
http://10.0.0.214:8093/send
```

with:

```http
X-Email-Relay-Key: <shared-key>
Content-Type: application/json
```

and a body like:

```json
{
  "subject": "Feather alert",
  "message": "The monitoring device detected an event."
}
```

The working firmware module is `EmailRelay.{cpp,h}`. It should return success only when the HTTP status is `200`.

Example use:

```cpp
EmailRelay Mailer("10.0.0.214", 8093, "/send", SECRET_EMAIL_RELAY_KEY);

if (Mailer.send("Feather test", "This is a test email from the Feather.")) {
  Log.println("EmailRelay test sent");
} else {
  Log.println("EmailRelay test failed");
}
```

---

## OMV Compose integration

The service is imported into OMV Compose so it appears in the OMV UI.

OMV path pattern:

```text
/srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/services-compose/email-relay/
```

OMV generates files such as:

```text
email-relay.yml
email-relay.env
compose.yml
.env
```

Those files are generated by OMV. Do not edit them directly.

Preferred source of truth:

```text
/home/joe/workspace/modemBounce/iot-services/email-relay
```

If `docker-compose.yml` or `.env` changes, update the repo first, then update/re-import through the OMV UI so OMV’s generated copy stays synchronized.

---

## Comparing repo and OMV copy

Compare Compose YAML:

```bash
sudo diff -u \
  /home/joe/workspace/modemBounce/iot-services/email-relay/docker-compose.yml \
  /srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/services-compose/email-relay/email-relay.yml
```

A difference consisting only of OMV’s generated header is expected.

Compare the shared key without printing it:

```bash
repo_key_hash="$(
  sed -n 's/^SHARED_KEY=//p' /home/joe/workspace/modemBounce/iot-services/email-relay/.env |
  sha256sum |
  awk '{print $1}'
)"

omv_key_hash="$(
  sudo sed -n 's/^SHARED_KEY=//p' /srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/services-compose/email-relay/email-relay.env |
  sha256sum |
  awk '{print $1}'
)"

if [ "$repo_key_hash" = "$omv_key_hash" ]; then
  echo "SHARED_KEY values match"
else
  echo "SHARED_KEY values differ"
fi
```

---

## Security notes

This service is designed for trusted LAN use.

Important limits:

- The Feather does not get SMTP credentials.
- The service is bound only to `10.0.0.214:8093`.
- The HTTP endpoint requires `X-Email-Relay-Key`.
- Recipients are fixed in `.env`.
- The service does not accept arbitrary recipient addresses from the request body.
- HTTPS is not used; this is acceptable only because the service is LAN-only.

Protect these files:

```text
.env
OMV-generated email-relay.env
```

They contain secrets.

---

## Rate limiting

The current service does not implement persistent rate limiting.

The Feather firmware should avoid sending repeated emails in a tight loop. For example:

- send only on state transitions
- enforce a minimum interval between alerts
- use logging for frequent status messages
- use email only for significant monitoring events

If needed later, `email_relay.py` can add in-memory rate limiting such as “no more than one email every N seconds.”

---

## Troubleshooting

### Container is not running

```bash
sudo docker ps -a --filter name=email-relay
sudo docker compose logs --tail=50
```

### Port is not listening

```bash
sudo ss -ltnp | grep 8093
```

Expected listener:

```text
10.0.0.214:8093
```

### Unauthorized request

Check that the Feather’s `SECRET_EMAIL_RELAY_KEY` matches `.env`:

```text
SHARED_KEY=...
```

Do not paste the key into logs or chat.

### SMTP failure

Look at Docker logs:

```bash
sudo docker compose logs --tail=50
```

Possible causes:

- wrong SMTP password/app password
- Gmail app password revoked
- wrong SMTP port/TLS setting
- network or DNS issue on `pi-nas`
- SMTP provider rate limiting or rejecting the sender

### Email delivered to only one recipient

`MAIL_TO` is comma-separated:

```text
MAIL_TO=person1@example.com,person2@example.com
```

Make sure there are no unexpected quotes around the value.

---

## Git notes

Files that should be committed:

```text
Dockerfile
docker-compose.yml
email_relay.py
.env.example
.gitignore
README.md
```

Files that should not be committed:

```text
.env
*.log
__pycache__/
```

Before committing, check for accidental secrets:

```bash
git status
git diff --cached
grep -RIn --exclude-dir=.git \
  -e 'SHARED_KEY=' \
  -e 'SMTP_PASS=' \
  -e 'SECRET_EMAIL_RELAY_KEY' \
  .
```

Expect `SHARED_KEY=` and `SMTP_PASS=` only in `.env` or examples with placeholder values. Do not commit real secret values.
