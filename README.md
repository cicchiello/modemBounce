# modemBounce

A home-network monitor that automatically power-cycles the cable modem or Wi-Fi router when connectivity problems are detected. The device periodically pings four targets — router, modem, an internet IP, and a DNS hostname. If any check fails repeatedly, the corresponding device is power-cycled through a normally-closed solid-state relay and an alert email is sent.

The hardware is an **Adafruit Feather M0** (SAMD21 + ATWINC1500 Wi-Fi), housed in a laser-cut enclosure. See `schematic.txt` for the circuit. An older Raspberry Pi proof-of-concept lives in `rpi/` but is not maintained.

---

## Hardware

### Bill of Materials

| Qty | Component | Notes |
|-----|-----------|-------|
| 1 | Adafruit Feather M0 WiFi (ATWINC1500) | MCU + Wi-Fi |
| 2 | Berm KB25DA normally-closed SSR | 3–32 V DC input, 25 A AC output |
| 2 | 2N2222 NPN transistor | Low-side SSR driver (one per SSR) |
| 2 | 2.2 kΩ resistor, 1/4 W | 2N2222 base resistor |
| 1 | RGB LED, 5 mm, common cathode | Status indicator |
| 1 | 470 Ω resistor, 1/4 W | LED common-cathode current-limiting |
| 1 | Tactile push button, normally open | Reset + factory-reset input (see schematic) |
| 1 | Capacitor | AC-couples button press to Feather RESET pin for momentary reset pulse |
| 1 | USB wall adapter (disassembled) | Wired internally to post-fuse AC; 5 V DC output wired directly to Feather VUSB header pin |
| 1 | Protoboard | Hand-assembled PCB carrying the Feather and all low-voltage components |

See `schematic.txt` for wiring details. The mains hot feed passes through a 10 A fuse before splitting to both SSRs. Neutral runs straight through to both outlets; only the hot conductor is switched.

### Enclosure

The enclosure is entirely laser-cut from **Delrin** (acetal/POM), chosen for its electrical insulation and self-extinguishing properties. All panels and internal dividers are Delrin; there are no metal structural parts.

The interior is divided into **three compartments** by two internal divider panels:

| Compartment | Contents |
|-------------|----------|
| Low-voltage | Protoboard PCB (Feather M0 + SSR drivers + LED + reset button) |
| High-voltage mixed | SSRs, disassembled USB wall-wart, incoming mains cable |
| AC outlets | Modem and router plugs, 10 A fuse |

The primary intent is HV/LV isolation; three compartments emerged naturally from the physical layout of the components.

FreeCAD source (`bouncy_v1.2.FCStd`) and laser-cut SVG panel exports are in `enclosure/svg-output/`. Interior divider profiles are `interior-short-v1.2.svg` and `interior-long-v1.2.svg`.

---

## Software components

| Location | Description |
|----------|-------------|
| `feather/bouncy/` | Arduino/C++ firmware (current implementation) |
| `iot-services/email-relay/` | LAN email-relay service, Docker, runs on pi-nas |
| `rpi/` | Legacy Raspberry Pi proof-of-concept — not maintained |

---

## Firmware setup

### Arduino IDE libraries

Install via *Sketch → Include Library → Manage Libraries* or as ZIP files:

- **WiFi101** — ATWINC1500 driver (Arduino)
- **FlashStorage_SAMD** — EEPROM emulation in SAMD21 flash
- **arduinolibs** suite by rweather — provides `Crypto`, `AES`, `CTR`, `SHA256`

Board: *Adafruit SAMD Boards → Adafruit Feather M0*

### Secrets file

Edit `feather/bouncy/arduino_secrets.h`:

| Symbol | Purpose |
|--------|---------|
| `SECRET_SSID` / `SECRET_PASS` | Home Wi-Fi creds — used only in non-PROD test modes for direct-connect |
| `LOG_HOST/PORT/PATH`, `SECRET_LOGGING_KEY` | HTTP log service on pi-nas (port 8091) |
| `EMAIL_HOST/PORT/PATH`, `SECRET_EMAIL_KEY` | email-relay service on pi-nas (port 8093) |
| `SECRET_WIFI_ENC_KEY` | 16-byte AES-128 key for flash credential storage |
| `SECRET_WIFI_MAC_KEY` | 32-byte HMAC-SHA256 key for flash credential authentication |

Generate random keys:
```bash
openssl rand -hex 16    # → SECRET_WIFI_ENC_KEY (format as byte array)
openssl rand -hex 32    # → SECRET_WIFI_MAC_KEY
openssl rand -hex 24    # → shared keys for logging and email-relay
```

The `SHARED_KEY` value in `iot-services/email-relay/.env` must match `SECRET_EMAIL_KEY`.

### Build and flash

1. Open `feather/bouncy/bouncy.ino` in Arduino IDE.
2. Select *Tools → Board → Adafruit Feather M0*.
3. Select the correct serial port.
4. Click **Upload**.

---

## First-time setup (provisioning)

On first boot — or after a factory reset — there are no stored Wi-Fi credentials. The device enters provisioning mode automatically.

1. **LED goes blinking-red.** The Feather broadcasts a Wi-Fi network named **`Bouncy-Setup`**.
2. Connect a phone or laptop to `Bouncy-Setup`.
3. Open a browser and go to **`http://192.168.1.1/`**.
4. Enter your home Wi-Fi SSID and password.
5. Choose a **run mode** — use *Normal production mode* for permanent installation.
6. Tap **Save WiFi Settings**. The LED goes blue as the device connects.
7. Disconnect from `Bouncy-Setup`. The Feather joins your home network, **LED turns green**, and monitoring begins.

Run mode applies to the current session only; the device boots into `PROD` mode if it has valid credentials persisted. Test modes are intended for bench testing.

---

## Factory reset

The reset button is AC-coupled through a capacitor to the Feather's RESET pin, so any press immediately reboots the device. On startup the firmware checks whether the button is still held (via pin **A0**, INPUT_PULLUP):

- **Brief press / release before 5 s** — normal reboot, credentials intact.
- **Hold for 5 s** — Stored Wi-Fi credentials are erased and the device re-enters provisioning mode (with flasing-red indicator).

---

## Normal operation

| LED | Meaning |
|-----|---------|
| Green | Connected and monitoring normally |
| Blue | Connecting / reconnecting to Wi-Fi |
| Red (flashing) | Provisioning mode — AP `Bouncy-Setup` is up; open `http://192.168.1.1` to configure |
| Red (steady) | Problem detected; bouncing a device |

The device checks connectivity every **30 minutes** (2 minutes in test modes):

1. Wi-Fi AP connection
2. Ping router — `10.0.0.1`
3. Ping modem — `192.168.100.1`
4. Ping internet — `8.8.8.8`
5. DNS resolution — `google.com`

Each failing check is retried up to **5 times** at 1-minute intervals. After 5 consecutive failures:

- Router ping failure → router is power-cycled
- Modem / internet / DNS failure → cable modem is power-cycled

After a bounce, the device waits **3.5 minutes** for the device to reinitialize, then sends an alert email and resumes monitoring.

---

## email-relay service

The email-relay is a small HTTP service on **pi-nas** (`10.0.0.214:8093`) that lets the Feather send alert emails without embedding SMTP credentials in the firmware. It runs in Docker.

See [`iot-services/email-relay/README.md`](iot-services/email-relay/README.md) for full setup, configuration, and testing instructions.
