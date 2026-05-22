# AI Network Badge

<p align="center">
  <img src="cricut/badge_preview.png" alt="AI Network Badge front: I NETWORK / WITH AI / BUILD THE MESH text with copper traces and 8 LED node rings on a dark circular background" width="360">
</p>

A self-contained ESP32-C3 conference badge. It hosts an open Wi-Fi captive portal so anyone who joins gets the badge web page automatically, drives an 8-pixel NeoPixel strip with four idle animations and seven scripted reaction effects, runs the "Build the Mesh" packet game with persistent score and level-ups, accepts contact card submissions through a web form and stores them in NVS, and quietly scans for nearby badges over BLE so two attendees with badges greet each other with a link-up animation.

## Features

- Open Wi-Fi SoftAP `AI-BADGE` with full captive portal (auto-launches the page on iOS, Android, and Windows).
- 8-pixel NeoPixel strip on GPIO 4 with four idle patterns: Packet Chase, AI Pulse, Network Sparkle, Rainbow Mesh.
- Seven reaction effects: PACKET, LINKUP, AI, STORM, GITHUB, LINKEDIN, CONTACT (4.5 s each).
- "Build the Mesh" game with levels Offline Node → Listening Node → Linked Node → Mesh Builder → AI Router → Supernode and a STORM celebration on every threshold cross.
- Contact card form (name / contact / note), input-sanitized and stored in NVS, capped at 25 entries, worth 3 packets per submission.
- BLE peer discovery: advertises as `AI-BADGE-MARSHALL`, scans every 20 s for 3 s, dedupes peers whose name starts with `AI-BADGE`, triggers LINKUP on a new peer.
- **Nearby Badges** section on the home page: each remembered peer shown with friendly RSSI label (Near / Far / Distant) plus raw dBm and first-seen relative time.
- **Recent Activity** feed on the home page: last 8 events (reactions, contacts, peer discoveries, level-ups) shown newest-first with relative timestamps. Volatile in RAM.
- **Per-badge admin key**: each badge boots with a 4-character hex key derived from the last 2 bytes of its chip MAC. Optionally override it via NVS using the USB Serial console (`setkey=<value>` / `clearkey`). Reveal the active key from the web UI at `/admin/key` only while the BOOT button is physically held.
- **Owner login page** at `/admin`: friendly password prompt that sends the owner to the contacts admin page or the web console, depending on the `?next=` target. Tapped from the home page **Owner: View Contacts** and **Owner: Console** buttons. Wrong key bounces back with an inline error.
- **Web console** at `/console`: terminal-styled page for running USB-serial commands (`setkey=`, `clearkey`, `factoryreset`) over Wi-Fi. Reuses the same command parser as the USB serial console. Gated by the active admin key — visiting the URL without a key sends the user through the `/admin` login flow first.
- **Factory reset**: type `factoryreset` over USB serial, or hold the BOOT button while plugging in USB for 5 seconds, to wipe the entire NVS namespace `badge` and reboot.
- **Friendly hostname**: the badge advertises itself over mDNS as `badge.local` so visitors can type that instead of `192.168.4.1`. Works on macOS, iOS, and modern Android — falls back to the captive portal otherwise.

This is mostly a fun side project, but also a great way to spark conversations with new people around networking, AI, and other interesting tech topics. Looking forward to seeing where it goes. as `badge.local` so visitors can type that instead of `192.168.4.1`. Works on macOS, iOS, and modern Android — falls back to the captive portal otherwise.
- BOOT button (GPIO 9) cycles idle patterns.
- Persistent settings (brightness, idle pattern, packet count, contact count, peer count) survive reboot via NVS namespace `badge`.
- Admin endpoints for listing, exporting (CSV), and clearing contacts, gated by the active per-badge admin key.

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
  - NimBLE-Arduino — install via Library Manager. Search for "NimBLE-Arduino" by h2zero. Smaller and faster than the bundled BLEDevice library; the badge ships with NimBLE for binary-size headroom.
  - ESP32 mDNS — bundled with the `espressif/arduino-esp32` core, no separate install needed.

## Project Layout

```
firmware/firmware.ino    # the Arduino sketch
cricut/                  # 4-inch round badge artwork (vinyl + back label + QR)
README.md
wiring.md
troubleshooting.md
.kiro/                   # spec docs (requirements / design / tasks)
```

## Configuration (Edit Before An Event)

Open `firmware/firmware.ino` and find the `CONFIG` block at the top of the file (between the `CONFIG — Edit values in this block to personalize the badge.` and `END CONFIG` banner comments). Update these six constants before handing the badge out:

- `BADGE_OWNER` — name shown on the badge web page.
- `BADGE_TITLE` — subtitle / role line under the owner name.
- `LINKEDIN_URL` — link for the "Connect on LinkedIn" button.
- `GITHUB_URL` — link for the "View GitHub" button.
- `BLE_BADGE_NAME` — local BLE advertising name; peers look for badges starting with the prefix below.
- `BLE_BADGE_PREFIX` — prefix used to detect peer badges during BLE scans.

Everything below the `END CONFIG` banner is implementation and shouldn't need editing.

### Admin Key (No Edit Needed)

There is intentionally no `ADMIN_KEY` constant in the CONFIG block. Each badge derives its key on first boot from its own chip MAC address (the last 2 bytes formatted as 4 lowercase hex characters), so most freshly flashed badges get a key that's both unique and short enough to type from the printed badge. The active key is printed to the USB Serial console at boot.

To override it with a custom value, plug the badge into a serial terminal at 115200 baud and type:

```
setkey=your-custom-key
```

To revert to the MAC-derived default:

```
clearkey
```

The override is stored in NVS and survives reboots until you `clearkey` it. There is no web form for setting the key — over-the-air admin-key rotation is intentionally not supported.

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
arduino-cli lib install "NimBLE-Arduino"
arduino-cli compile --fqbn esp32:esp32:esp32c3 firmware
arduino-cli upload --fqbn esp32:esp32:esp32c3 -p COM3 firmware
```

Replace `COM3` with the actual port on your system (e.g. `/dev/ttyUSB0` on Linux, `/dev/cu.usbmodem*` on macOS).

## First Boot Checklist

- Power the badge over USB.
- Open a serial terminal at 115200 baud and note the **active admin key** printed in the boot banner. It looks like `Admin contacts: http://192.168.4.1/contacts?key=cd34`.
- Look for Wi-Fi network `AI-BADGE` (open, no password) on your phone or laptop.
- Joining should auto-launch the badge page; if not, open `http://192.168.4.1` or `http://badge.local` in any browser.
- Confirm the new home-page sections render: **Nearby Badges** (empty until a second badge appears) and **Recent Activity** (should show the boot reaction shortly after).
- Try the LED reaction buttons (Send Packet, Establish Link, etc.) and watch the strip respond and the activity feed update.
- Submit a contact card and confirm it adds +3 packets to your count and shows up under Recent Activity.
- Admin URL: `http://192.168.4.1/contacts?key=<your-key>`. CSV export: `http://192.168.4.1/contacts.csv?key=<your-key>`.
- Or use the owner login page: tap **Owner: View Contacts** or **Owner: Console** on the badge home page (or visit `http://192.168.4.1/admin`), enter your key, and submit. The login form routes to whichever admin destination the link requested.
- If you don't have the serial cable handy, hold the BOOT button on the badge and visit `http://192.168.4.1/admin/key` from your phone — it returns the active key in plain text only while the button is held.
- Press the BOOT button on the board to cycle idle patterns.

## Resetting a Badge

The badge keeps everything in NVS namespace `badge`: brightness, idle pattern, mesh score, peer count, stored contacts, and the custom admin key (if set). Three levels of reset:

| Level | Trigger | What it clears |
| ----- | ------- | -------------- |
| Mesh score | `http://192.168.4.1/resetcount` | Packets and peer count |
| Stored contacts | `http://192.168.4.1/clearcontacts?key=<your-key>` | All contact entries |
| Custom admin key | `clearkey` over USB serial | Key override (reverts to MAC default) |
| **Full factory reset** | `factoryreset` over USB serial, **or** hold BOOT for 5 s while plugging in | Everything in the `badge` namespace |

To do a full factory reset without a serial cable:

1. Unplug USB.
2. Press and hold the BOOT button on the board.
3. Plug USB back in while still holding.
4. The LED strip pulses red and ramps up in brightness as the hold progresses.
5. Keep holding for **5 seconds**. The badge wipes NVS and reboots automatically.
6. Release the button at any point before 5 seconds to abort — the badge boots normally.

The reset only touches the `badge` namespace. System-level NVS (Wi-Fi credentials, BLE bonding) is preserved. For a true full-flash erase (e.g. recovering from a bricked sketch), see `troubleshooting.md`.

## HTTP Endpoints

All routes are served from `http://192.168.4.1/` while connected to the badge's `AI-BADGE` Wi-Fi network.

| Method | Path                  | Auth                  | What it does |
| ------ | --------------------- | --------------------- | ------------ |
| GET    | `/`                   | none                  | Badge home page (identity, mesh game, Nearby Badges, Recent Activity, reaction buttons, contact form, controls) |
| GET    | `/trigger?fx=<name>`  | none                  | Fires a reaction effect: `packet`, `linkup`, `ai`, `storm`, `github`, `linkedin` |
| POST   | `/contact`            | none                  | Submits a contact card (name / contact / note) and adds +3 packets |
| GET    | `/next`               | none                  | Cycles to the next idle pattern |
| GET    | `/brightness?b=<n>`   | none                  | Sets LED brightness (clamped 5–120) |
| GET    | `/resetcount`         | none                  | Resets mesh score and peer count |
| GET    | `/admin`              | none (login page)     | Owner login form. Pass `?next=/console` to log in for the web console (default `/contacts`). |
| GET    | `/admin/key`          | BOOT button held      | Returns the active admin key in plain text only while BOOT is pressed |
| GET    | `/console`            | admin key             | Web serial console: type `setkey=`, `clearkey`, or `factoryreset` from a browser |
| GET    | `/contacts?key=`      | admin key             | HTML list of stored contacts |
| GET    | `/contacts.csv?key=`  | admin key             | CSV download of stored contacts |
| GET    | `/clearcontacts?key=` | admin key             | Wipes all stored contacts |

The badge also intercepts the standard Apple, Android, and Windows captive-portal probe URLs and redirects them to the home page.

## USB Serial Commands

Open a serial terminal at **115200 baud** and type one of these commands followed by Enter.

| Command | What it does |
| ------- | ------------ |
| `setkey=<value>` | Stores `<value>` as the custom admin key in NVS and prints confirmation. Empty values are rejected. |
| `clearkey` | Removes the custom admin key and reverts to the MAC-derived default. Prints the new active key. |
| `factoryreset` | Wipes the entire `badge` NVS namespace and reboots the badge. |

The same commands also work from the **web console** at `http://192.168.4.1/console` (or `http://badge.local/console`) without a USB cable. The web console gates on the active admin key and reuses the same command parser; output is mirrored to a small in-memory log shown on the page. From the home page, tap **Owner: Console** to get there through the login form.

Unknown commands are echoed as `Unknown command: <line>` so typos are visible. Lines longer than 96 bytes are truncated at the next newline.

## Where Things Live

- Hardware diagrams, pin references, and power notes: `wiring.md`.
- Front vinyl, LED placement guide, and back label artwork for a 4-inch acrylic badge: `cricut/`.
- Common failure modes and recovery steps: `troubleshooting.md`.

## License / Credits

Personal project. Use at your own risk.
