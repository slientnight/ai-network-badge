# AI Network Badge

A self-contained ESP32-C3 conference badge. It hosts an open Wi-Fi captive portal so anyone who joins gets the badge web page automatically, drives an 8-pixel NeoPixel strip with four idle animations and seven scripted reaction effects, runs the "Build the Mesh" packet game with persistent score and level-ups, accepts contact card submissions through a web form and stores them in NVS, and quietly scans for nearby badges over BLE so two attendees with badges greet each other with a link-up animation.

## Features

- Open Wi-Fi SoftAP `AI-BADGE` with full captive portal (auto-launches the page on iOS, Android, and Windows).
- 8-pixel NeoPixel strip on GPIO 4 with four idle patterns: Packet Chase, AI Pulse, Network Sparkle, Rainbow Mesh.
- Seven reaction effects: PACKET, LINKUP, AI, STORM, GITHUB, LINKEDIN, CONTACT (4.5 s each).
- "Build the Mesh" game with levels Offline Node → Listening Node → Linked Node → Mesh Builder → AI Router → Supernode and a STORM celebration on every threshold cross.
- Contact card form (name / contact / note), input-sanitized and stored in NVS, capped at 25 entries, worth 3 packets per submission.
- BLE peer discovery: advertises as `AI-BADGE-MARSHALL`, scans every 20 s for 3 s, dedupes peers whose name starts with `AI-BADGE`, triggers LINKUP on a new peer.
- BOOT button (GPIO 9) cycles idle patterns.
- Persistent settings (brightness, idle pattern, packet count, contact count, peer count) survive reboot via NVS namespace `badge`.
- Admin endpoints for listing, exporting (CSV), and clearing contacts, gated by a soft `?key=` query parameter.

## Supported Boards

- Seeed XIAO ESP32-C3
- ESP32-C3 SuperMini

Both boards are pin-compatible for this firmware: GPIO 4 drives the LED data line and GPIO 9 is the onboard BOOT button.

## Prerequisites

- Arduino IDE 2.x, or Arduino CLI 1.x.
- ESP32 board package, installed via the Boards Manager URL:

  ```
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  ```

- Board selection per target:
  - Seeed XIAO ESP32-C3: `XIAO_ESP32C3` (or `ESP32C3 Dev Module` if Seeed variants aren't available in your installed core).
  - ESP32-C3 SuperMini: `ESP32C3 Dev Module`.
- Required libraries:
  - Adafruit NeoPixel — install via Library Manager.
  - ESP32 BLE — bundled with the `espressif/arduino-esp32` core, no separate install needed.

## Project Layout

```
firmware/
  firmware.ino   # the sketch
README.md
wiring.md
troubleshooting.md
```

## Configuration (Edit Before An Event)

Open `firmware/firmware.ino` and find the `CONFIG` block at the top of the file (between the `CONFIG — Edit values in this block to personalize the badge.` and `END CONFIG` banner comments). Update these seven constants before handing the badge out:

- `BADGE_OWNER` — name shown on the badge web page.
- `BADGE_TITLE` — subtitle / role line under the owner name.
- `LINKEDIN_URL` — link for the "Connect on LinkedIn" button.
- `GITHUB_URL` — link for the "View GitHub" button.
- `BLE_BADGE_NAME` — local BLE advertising name; peers look for badges starting with the prefix below.
- `BLE_BADGE_PREFIX` — prefix used to detect peer badges during BLE scans.
- `ADMIN_KEY` — query-string key gating `/contacts`, `/contacts.csv`, and `/clearcontacts` (soft gate, not real auth — change before each event).

Everything below the `END CONFIG` banner is implementation and shouldn't need editing.

## Build and Flash

### Arduino IDE 2.x

1. Open `firmware/firmware.ino`.
2. Tools → Board → select your target (`XIAO_ESP32C3` or `ESP32C3 Dev Module`).
3. Tools → Port → select the COM port the badge enumerated as.
4. Click Upload.

### Arduino CLI

```
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit NeoPixel"
arduino-cli compile --fqbn esp32:esp32:esp32c3 firmware
arduino-cli upload --fqbn esp32:esp32:esp32c3 -p COM3 firmware
```

Replace `COM3` with the actual port on your system (e.g. `/dev/ttyUSB0` on Linux, `/dev/cu.usbmodem*` on macOS).

## First Boot Checklist

- Power the badge over USB.
- Look for Wi-Fi network `AI-BADGE` (open, no password) on your phone or laptop.
- Joining should auto-launch the badge page; if not, open `http://192.168.4.1` in any browser.
- Try the LED reaction buttons (Send Packet, Establish Link, etc.) and watch the strip respond.
- Submit a contact card and confirm it adds +3 packets to your count.
- Admin URL: `http://192.168.4.1/contacts?key=meshadmin` (or whatever `ADMIN_KEY` you set). CSV export: `http://192.168.4.1/contacts.csv?key=meshadmin`.
- Press the BOOT button on the board to cycle idle patterns.

## Where Things Live

- Hardware diagrams, pin references, and power notes: `wiring.md`.
- Common failure modes and recovery steps: `troubleshooting.md`.

## License / Credits

Personal project. Use at your own risk.
