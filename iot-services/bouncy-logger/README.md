# modemBounce bouncy-logger

`modemBounce/iot-services/bouncy-logger` is a very small HTTP logging service to be run on `pi-nas`.

It accepts HTTP `PUT` requests from LAN devices, checks a shared secret key, and appends the request body to a log file stored on the NAS RAID volume.

The original target client is an Arduino-like device that periodically sends a short string to be logged.

---

## Host environment

This service is intended to run on:

- Host: `pi-nas`
- Hardware: Raspberry Pi 5
- OS: Debian / Raspberry Pi OS based OMV host
- NAS manager: OpenMediaVault
- Docker: installed through OMV Extras / OMV Compose plugin
- LAN IP: `10.0.0.214`
- Service port: `8091`

The service listens on:

```text
http://10.0.0.214:8091/log
```

Only HTTP `PUT` is accepted.

---

## Repository layout

```text
~/workspace/modemBounce/iot-services/bouncy-logger/
  Dockerfile
  docker-compose.yml
  logger.py
  .env              # local secret; not committed
  .gitignore
  README.md
```

There is also a symlink:

```text
/opt/pi-nas-services/bouncy-logger -> /home/joe/workspace/modemBounce/iot-services/bouncy-logger
```

The git repo is the preferred authoritative source for the service.

---

## Runtime data location

The log file is stored on the NAS RAID volume here:

```text
/srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/services-data/bouncy-logger/bouncy.log
```

Inside the Docker container, that directory is mounted as:

```text
/data
```

The logger writes to:

```text
/data/bouncy.log
```

This keeps log data off the Raspberry Pi SD card.

---

## Security model

This service is intentionally simple and LAN-only.

It does not use HTTPS.

Each request must include a shared key in the HTTP header:

```http
X-Log-Key: <shared-key>
```

The key is stored locally in `.env`:

```text
SHARED_KEY=...
```

The `.env` file is intentionally ignored by git.

Do not commit `.env`.

Do not put the key in the URL. Use the HTTP header instead.

---

## Current Docker Compose configuration

The service is defined in `docker-compose.yml`.

Important settings:

```yaml
ports:
  - "10.0.0.214:8091:8091"
```

This binds the service to the NAS LAN IP instead of all interfaces.

```yaml
restart: unless-stopped
```

This causes the container to restart automatically after reboot, unless it was manually stopped.

```yaml
volumes:
  - /srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/services-data/bouncy-logger:/data
```

This stores logs on the RAID-backed NAS volume.

---

## Basic commands

Run these from the repo directory:

```bash
cd ~/workspace/modemBounce/iot-services/bouncy-logger
```

### Show running container

```bash
sudo docker ps --filter name=bouncy-logger
```

### Start or rebuild service

```bash
sudo docker compose up -d --build
```

Use this after changing `logger.py`, `Dockerfile`, or `docker-compose.yml`.

### Stop service

```bash
sudo docker compose down
```

Note: because the restart policy is `unless-stopped`, a container stopped with `docker compose down` will not automatically come back until started again.

### View container logs

```bash
sudo docker compose logs
```

Follow logs live:

```bash
sudo docker compose logs -f
```

### Check restart policy

```bash
sudo docker inspect -f '{{.HostConfig.RestartPolicy.Name}}' bouncy-logger
```

Expected output:

```text
unless-stopped
```

---

## Testing

### Unauthorized request test

This should fail with HTTP `401 Unauthorized`:

```bash
curl -i \
  -X PUT \
  -H 'X-Log-Key: wrong-key' \
  --data 'this should be rejected' \
  http://10.0.0.214:8091/log
```

Expected result:

```text
HTTP/1.0 401 Unauthorized
```

### Authorized request test

This sends a test log message without displaying the key:

```bash
curl -i \
  -X PUT \
  -H "X-Log-Key: $(sed -n 's/^SHARED_KEY=//p' .env)" \
  --data 'test log message from curl' \
  http://10.0.0.214:8091/log
```

Expected result:

```text
HTTP/1.0 204 No Content
```

### Verify the log file

```bash
sudo tail -5 /srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/services-data/bouncy-logger/bouncy.log
```

Example log line:

```text
2026-05-27T14:56:12+00:00 10.0.0.x test log message from curl
```

Each line includes:

```text
UTC timestamp
source IP address
message body
```

---

## Arduino request shape

The Arduino should send an HTTP `PUT` request to:

```text
http://10.0.0.214:8091/log
```

With header:

```text
X-Log-Key: <shared-key>
```

And a short body string, for example:

```text
event=bounce count=123
```

Pseudo-code shape:

```cpp
http.begin("http://10.0.0.214:8091/log");
http.addHeader("X-Log-Key", "<shared-key>");
int rc = http.PUT("event=bounce count=123");
```

Expected success response:

```text
204 No Content
```

---

## OMV Compose UI integration

The service was originally created and tested from the command line in:

```text
/home/joe/workspace/modemBounce/iot-services/bouncy-logger
```

It was then imported into the OMV Compose plugin so OMV can display/manage it.

OMV Compose currently has its own generated copy under the `pi-nas` shared folder:

```text
/srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/bouncy-logger/bouncy-logger.yml
/srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/bouncy-logger/bouncy-logger.env
```

Those files are generated by OMV.

Do not edit the OMV-generated `.yml` directly. It contains a warning similar to:

```text
This file is auto-generated by openmediavault.
WARNING: Do not edit this file, your changes will get lost.
```

### Source of truth

The preferred source of truth is this git repo:

```text
/home/joe/workspace/modemBounce/iot-services/bouncy-logger
```

OMV's copy is useful for display/status and optional start/stop operations.

If the Compose file or environment changes, update the repo first, then update/re-import through the OMV UI so the OMV copy stays synchronized.

---

## Checking whether repo and OMV copy match

Compare Compose definitions:

```bash
sudo diff -u \
  /home/joe/workspace/modemBounce/iot-services/bouncy-logger/docker-compose.yml \
  /srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/bouncy-logger/bouncy-logger.yml
```

A difference consisting only of OMV's generated comment header is expected.

Compare environment files without printing the secret:

```bash
sudo cmp -s \
  /home/joe/workspace/modemBounce/iot-services/bouncy-logger/.env \
  /srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/bouncy-logger/bouncy-logger.env \
  && echo "env files match" \
  || echo "env files differ"
```

Note: OMV's env file may contain comment headers, so a literal `cmp` may report differences even when the actual `SHARED_KEY=...` value matches.

To compare only the key values without displaying them:

```bash
repo_key_hash="$(
  sed -n 's/^SHARED_KEY=//p' /home/joe/workspace/modemBounce/iot-services/bouncy-logger/.env |
  sha256sum |
  awk '{print $1}'
)"

omv_key_hash="$(
  sudo sed -n 's/^SHARED_KEY=//p' /srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/bouncy-logger/bouncy-logger.env |
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

## Rotating the shared key

Generate a new key:

```bash
openssl rand -hex 24
```

Edit `.env`:

```bash
nano .env
```

Update:

```text
SHARED_KEY=<new-key>
```

Restart the service from the repo:

```bash
sudo docker compose up -d --build
```

Then update/re-import the OMV Compose entry through the OMV UI so OMV's stored environment matches.

After rotating the key, update the Arduino/device configuration.

---

## Docker storage note

Docker's image/container storage is currently:

```text
/var/lib/docker
```

This is on the Raspberry Pi SD card.

This is acceptable for this small service because:

- the image is small
- the container does not write significant data to its writable layer
- the log file is bind-mounted to the RAID volume

If many more containers are added later, consider moving Docker storage to the NAS volume or another suitable disk, but that is intentionally not done for this lightweight service.

---

## Log rotation

The current service appends to:

```text
bouncy.log
```

No application-level log rotation is implemented yet.

For low-rate Arduino logging, this may be fine for a long time.

If the log becomes large, add host-side `logrotate` later for:

```text
/srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/services-data/bouncy-logger/bouncy.log
```

Example future policy:

```text
rotate weekly
keep several compressed old logs
do not rotate if empty
create new log with appropriate ownership
```

---

## Troubleshooting

### Container is not running

```bash
sudo docker ps -a --filter name=bouncy-logger
```

Then inspect logs:

```bash
sudo docker compose logs
```

### Port not listening

```bash
sudo ss -ltnp | grep 8091
```

Expected listener:

```text
10.0.0.214:8091
```

### Bad key test succeeds unexpectedly

That should not happen. Check the `.env` file and Compose environment:

```bash
sudo docker inspect bouncy-logger | grep -A20 Env
```

Be careful: this may display the shared key.

### Good key test fails

Check that `.env` exists and contains `SHARED_KEY`:

```bash
ls -l .env
grep '^SHARED_KEY=' .env
```

Be careful when copying output because this displays the key.

### Log file not written

Check that the data directory exists:

```bash
sudo ls -ld /srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/services-data/bouncy-logger
```

Check the log file:

```bash
sudo ls -l /srv/dev-disk-by-uuid-1b03c883-24b8-4bbc-9e3f-9fdc70389470/pi-nas/services-data/bouncy-logger/bouncy.log
```

Check container logs:

```bash
sudo docker compose logs
```

---

## Git notes

Files that should be committed:

```text
Dockerfile
docker-compose.yml
logger.py
.gitignore
README.md
```

Files that should not be committed:

```text
.env
*.log
__pycache__/
```

Check status:

```bash
git status
```

Initialize repo, if not already done:

```bash
git init
git add Dockerfile docker-compose.yml logger.py .gitignore README.md
git commit -m "Initial bouncy logger service"
```
