# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

**modemBounce** is a home network monitoring system that automatically power-cycles the cable modem and/or router when connectivity problems are detected. It has three components that evolved over time:

1. **`feather/bouncy/`** — Arduino/C++ firmware for an Adafruit Feather M0 (ATWINC1500 WiFi). The primary, current implementation.
2. **`rpi/`** — Legacy Python script for a Raspberry Pi, run via cron. Superseded by the Feather.
3. **`iot-services/email-relay/`** — Small Python HTTP service running on `pi-nas` (Raspberry Pi 5 / OpenMediaVault at `10.0.0.214`). Acts as an SMTP relay so the Feather does not need SMTP credentials.
4. **`enclosure/`** — Laser-cut Delrin enclosure. Three internal compartments: low-voltage PCB | SSRs + wall-wart + mains input | AC plugs + fuse. FreeCAD source and SVG panel/divider exports.

## Building and deploying

### Feather firmware

Built and flashed with **Arduino IDE**. There is no CLI build system. Open `feather/bouncy/bouncy.ino` in the Arduino IDE, select the Adafruit Feather M0 board, and upload.

Secrets are in `feather/bouncy/arduino_secrets.h` — IPs, shared keys, and the AES/HMAC keys used to encrypt WiFi credentials in flash.

### email-relay service (on pi-nas)

```bash
cd iot-services/email-relay
sudo docker compose up -d --build   # start or rebuild
sudo docker compose down             # stop
sudo docker compose logs -f          # follow logs
```

The service binds to `10.0.0.214:8093`. Configuration lives in `.env` (not committed). Copy from `.env.example` and generate a shared key with `openssl rand -hex 24`.

Test the running service:
```bash
# Should return 401:
curl -i -X POST -H 'X-Email-Relay-Key: wrong-key' \
  -H 'Content-Type: application/json' \
  --data '{"subject":"test","message":"rejected"}' http://10.0.0.214:8093/send

# Should return 200 (reads key from .env without printing it):
curl -i -X POST -H "X-Email-Relay-Key: $(sed -n 's/^SHARED_KEY=//p' .env)" \
  -H 'Content-Type: application/json' \
  --data '{"subject":"test","message":"ok"}' http://10.0.0.214:8093/send
```

## Feather firmware architecture

### State machine

`bouncy.ino` runs a cooperative state machine in `loop()`:

```
Initialize → APConnect → RouterPing → ModemPing → InternetPing → DnsPing → Sleep → (repeat)
```

The `Wait` state counts down `sWait_ms` by 1ms per loop tick, flushing the HTTP log buffer each tick. After each successful ping sequence the device sleeps for 30 minutes (PROD) or 2 minutes (test mode).

### Failure/bounce logic

Each ping state retries up to `NUM_FAILURES_TO_BOUNCE` times (5 in PROD, 3 in test) with 60-second delays between tries. When retries are exhausted:
- Router ping failure → `bounce_router()` (GPIO pin 5)
- Modem/Internet/DNS ping failure → `bounce_modem()` (GPIO pin A5)

**SSR wiring is normally-closed**: `LOW` = relay closed = power **on**; `HIGH` = relay open = power **off**.

Bounce sequence: power off → 15 s → power on → wait 210 s for device initialization → set `sPendingEmail`. The pending email is sent during the next `Wait` drain cycle.

### GPIO pin assignments

| Pin | Function |
|-----|----------|
| A0  | Factory-reset button (INPUT_PULLUP; hold 5 s to clear stored WiFi creds) |
| A5  | Modem SSR (normally-closed) |
| 5   | Router SSR (normally-closed) |
| 8,7,4,2 | ATWINC1500 SPI/control pins (set via `WiFi.setPins`) |

### RGB LED status

| Color | Meaning |
|-------|---------|
| Green | Connected and healthy |
| Blue  | Connecting / reconnecting to WiFi |
| Red (flashing) | Provisioning / AP mode — waiting for WiFi credentials at `http://192.168.1.1` |
| Red (flashing, reset held) | Factory-reset triggered after 5s |
| Red (steady) | Bouncing a device or error |

### Component classes

- **`WifiCredStore`** — Persists WiFi credentials in flash (emulated EEPROM). Encrypted with AES-128-CTR + HMAC-SHA256 using keys from `arduino_secrets.h`. Magic number `0x57464352` ("WFCR") and version field guard against corrupted reads.
- **`Provision`** — Captive-portal provisioning server. When no credentials exist in flash, the Feather starts a WiFi AP named `Bouncy-Setup`, serves a setup form at `/`, and accepts `POST /save`. The form also sets the `Runtype` (test mode). Credentials are stored via `WifiCredStore`.
- **`Connector`** — WiFi connection manager. `connectUsingStoredCreds()` loops forever until connected; calls `Provision` if no credentials are stored. `reconnect()` tears down and re-establishes the connection using the last-used credentials.
- **`HttpLogger`** — Sends log lines to `http://LOG_HOST:LOG_PORT/LOG_PATH` with an `X-Log-Key` header. Buffers output before WiFi is available.
- **`Logger`** — Facade over `HttpLogger`. Mode is `LOG_TO_HTTP` in PROD, `LOG_TO_BOTH` (serial + HTTP) in test modes.
- **`EmailRelay`** — Sends a `POST /send` to the email-relay service with `X-Email-Relay-Key` header and a JSON body `{"subject":..., "message":...}`.
- **`IndicatorLED`** — Wraps the RGB LED. State is tracked in `mR/mG/mB` booleans; actual pin writes happen in the setters.

### Test modes

Test mode is selected via the "Run mode" radio buttons on the provisioning page. `sTest` is a static variable initialized to `PROD` on every boot; the provisioning form can change it for that session only — it is not persisted to flash and resets to `PROD` on the next reboot. Available modes:

| Mode | Effect |
|------|--------|
| `PROD` | Normal operation |
| `test_PROD` | Production path but logs to both serial and HTTP |
| `test_ROUTER` | RouterIP set to an unreachable address |
| `test_MODEM` | ModemIP set to an unreachable address |
| `test_IP` | Internet IP set to an unreachable address |
| `test_DNS` | DNS hostname set to a non-existent name |
| `test_BOUNCE_MODEM` | Immediately triggers `bounce_modem()` in `setup()` |
| `test_BOUNCE_ROUTER` | Immediately triggers `bounce_router()` in `setup()` |
| `test_EMAIL` | Sends a test email in `setup()` |

## email-relay service architecture

Single-file Python 3 HTTP server (`email_relay.py`) using `ThreadingHTTPServer`. All configuration via environment variables loaded from `.env`. Only `POST /send` is handled. Authentication is a shared key in the `X-Email-Relay-Key` header. SMTP recipients are fixed in `.env` — the Feather cannot choose recipients. Supports both STARTTLS (port 587) and SMTP-over-SSL (port 465).

The service is imported into OMV Compose so it appears in the OpenMediaVault UI. The repo is the authoritative source; after editing `docker-compose.yml` or `.env`, update the OMV copy through the OMV UI.

## Network addresses (LAN)

| Host | IP | Purpose |
|------|----|---------|
| Router | 10.0.0.1 | Ping target |
| Modem | 192.168.100.1 | Ping target |
| pi-nas | 10.0.0.214 | Runs logging service (port 8091) and email-relay (port 8093) |
| Internet check | 8.8.8.8 | Google DNS IP |
| DNS check | google.com | DNS resolution test |

## Secrets

`feather/bouncy/arduino_secrets.h` contains WiFi SSID/password (used for test-mode direct connect only; PROD uses provisioned creds from flash), log service key, email-relay key, and the AES/HMAC keys for flash credential encryption.

`iot-services/email-relay/.env` contains the email-relay shared key and SMTP credentials. This file is gitignored and must not be committed.
