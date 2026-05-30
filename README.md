# AI Network Badge

<p align="center">
  <img src="badge_photo.jpeg" alt="AI Network Badge: round acrylic disc with holographic I NETWORK WITH AI text and blue LEDs, clipped to a lanyard" width="360">
</p>

A self-contained ESP32-C3 conference badge. It hosts an open Wi-Fi captive portal so anyone who joins gets the badge web page automatically, drives a 4-pixel NeoPixel strip with four idle animations and seven scripted reaction effects, runs the "Build the Mesh" packet game with persistent score and level-ups, accepts contact card submissions through a web form and stores them in NVS, and quietly scans for nearby badges over BLE so two attendees with badges greet each other with a link-up animation.

## Features

- Open Wi-Fi SoftAP with a per-badge unique SSID (e.g. `AI-BADGE-A3F7`) derived from the chip MAC, with full captive portal (auto-launches the page on iOS, Android, and Windows).
- 4-pixel NeoPixel strip on GPIO 4 with four idle patterns: Packet Chase, AI Pulse, Network Sparkle (default), Rainbow Mesh.
- Seven reaction effects: PACKET, LINKUP, AI, STORM, GITHUB, LINKEDIN, CONTACT (4.5 s each).
- "Build the Mesh" game with levels Offline Node → Listening Node → Linked Node → Mesh Builder → AI Router → Supernode and a STORM celebration on every threshold cross.
- Contact card form (name / contact / note), input-sanitized and stored in NVS, capped at 25 entries, worth 3 packets per submission.
- BLE peer discovery: advertises with a per-badge unique name (e.g. `AI-BADGE-A3F7`) derived from the chip MAC, scans every 20 s for 3 s, dedupes peers whose name starts with `AI-BADGE`, triggers LINKUP on a new peer.
- **Nearby Badges** section on the home page: shows active peers (seen in the last 60 s) with RSSI label and last-seen time. Stale peers are dimmed and grouped under "Previously seen."
- **Mesh Leaderboard** on the home page: ranks this badge and all nearby peers by packet count via BLE GATT score sync. Peers exchange scores automatically every 45 s.
- **Peer-aware idle mode**: when at least one BLE peer is nearby, LED 0 pulses a subtle green accent on top of the active idle pattern — a passive "you're not alone" indicator.
- **Recent Activity** feed on the home page: last 8 events (reactions, contacts, peer discoveries, level-ups) shown newest-first with relative timestamps. Volatile in RAM.
- **Per-badge unique identifiers**: the Wi-Fi SSID, BLE advertising name, and admin key are all derived from the chip's burned-in MAC address. Every badge is unique out of the box — no configuration needed. Override any of them in the CONFIG block (see code comments for details).
- **Owner login page** at `/admin`: friendly password prompt that sends the owner to the contacts admin page or the web console, depending on the `?next=` target. Tapped from the home page **Owner: View Contacts** and **Owner: Console** buttons. Wrong key bounces back with an inline error.
- **Web console** at `/console`: terminal-styled page for running USB-serial commands (`setkey=`, `clearkey`, `factoryreset`) over Wi-Fi. Reuses the same command parser as the USB serial console. Gated by the active admin key — visiting the URL without a key sends the user through the `/admin` login flow first.
- **Factory reset**: type `factoryreset` over USB serial, or hold the BOOT button while plugging in USB for 5 seconds, to wipe the entire NVS namespace `badge` and reboot.
- **Friendly hostname**: the badge advertises itself over mDNS as `badge.local` so visitors can type that instead of `192.168.4.1`. Works on macOS, iOS, and modern Android — falls back to the captive portal otherwise.
- **CPU die temperature** shown on the web page in °F and °C (internal sensor, not ambient — useful for diagnostics).
- **Underclocked to 80 MHz** to reduce power draw and die temperature. 80 MHz is the minimum for Wi-Fi; all badge functions remain unaffected.
- BOOT button (GPIO 9) cycles idle patterns on short press. **Long press (3 seconds)** fades LEDs and enters deep sleep (~5 µA). Press BOOT again to wake and reboot.
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

Open `firmware/firmware.ino` and find the `CONFIG` block at the top of the file (between the `CONFIG — Edit values in this block to personalize the badge.` and `END CONFIG` banner comments). Update these constants before handing the badge out:

- `BADGE_OWNER` — name shown on the badge web page.
- `BADGE_TITLE` — subtitle / role line under the owner name.
- `LINKEDIN_URL` — link for the "Connect on LinkedIn" button.
- `GITHUB_URL` — link for the "View GitHub" button.
- `BLE_BADGE_NAME_OVERRIDE` — custom BLE advertising name. Leave empty (`""`) to auto-generate a unique name from the chip MAC (e.g. `AI-BADGE-A3F7`).
- `BLE_BADGE_PREFIX` — prefix used to detect peer badges during BLE scans.
- `AP_SSID_OVERRIDE` — custom Wi-Fi SSID. Leave empty (`""`) to auto-generate from the chip MAC (e.g. `AI-BADGE-A3F7`).

Everything below the `END CONFIG` banner is implementation and shouldn't need editing.

### Per-Badge Unique Identifiers (No Edit Needed)

By default, both the Wi-Fi SSID and BLE advertising name are automatically derived from the chip's burned-in MAC address — the last 2 bytes formatted as 4 uppercase hex characters, appended to `AI-BADGE-`. This means every badge gets a unique network name out of the box with no configuration required.

To override either value with a custom string, set `AP_SSID_OVERRIDE` or `BLE_BADGE_NAME_OVERRIDE` in the CONFIG block. See the code comments for details.

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
3. Tools → Partition Scheme → select **Huge APP (3MB No OTA/1MB SPIFFS)**. The firmware with NimBLE exceeds the default partition size.
4. **ESP32C3 Dev Module only**: Tools → USB CDC On Boot → **Enabled**. Without this, `Serial` output goes to the hardware UART pins instead of the USB port, and you won't see anything in the serial monitor. (The XIAO_ESP32C3 board profile handles this automatically.)
5. Tools → Port → select the COM port the badge enumerated as.
6. Click Upload.

### Arduino CLI

```
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit NeoPixel"
arduino-cli lib install "NimBLE-Arduino"
arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app firmware
arduino-cli upload --fqbn esp32:esp32:esp32c3 -p COM3 firmware
```

Replace `COM3` with the actual port on your system (e.g. `/dev/ttyUSB0` on Linux, `/dev/cu.usbmodem*` on macOS).

## First Boot Checklist

- Power the badge over USB.
- Open a serial terminal at 115200 baud and note the **active admin key** printed in the boot banner. It looks like `Admin contacts: http://192.168.4.1/contacts?key=cd34`.
- Look for the badge's Wi-Fi network on your phone or laptop. It will be named `AI-BADGE-XXXX` where XXXX is derived from the chip MAC (open, no password). The exact name is printed to the serial console at boot.
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

All routes are served from `http://192.168.4.1/` while connected to the badge's Wi-Fi network (named `AI-BADGE-XXXX` where XXXX is the MAC-derived suffix).

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

## Technical Notes

- **Partition scheme**: You must select **Huge APP (3MB No OTA/1MB SPIFFS)** in Arduino IDE (or pass `PartitionScheme=huge_app` via CLI). The firmware with NimBLE exceeds the default partition size.
- **CPU frequency**: The badge runs at 80 MHz (set via `setCpuFrequencyMhz(80)` in `setup()`). This is the minimum clock that supports Wi-Fi and cuts die temperature significantly. If you need more CPU headroom for custom additions, you can change this to 160 MHz.
- **BLE GATT leaderboard**: Each badge exposes its packet count as a readable BLE characteristic. Nearby badges connect as GATT clients to read each other's scores. This happens in a round-robin fashion (one peer every ~10 s) to avoid blocking the main loop.
- **Peer timeout**: A peer is considered "nearby" if it was seen in the last 60 seconds (roughly 3 BLE scan cycles). After that it fades from the leaderboard and the Nearby Badges active list.
- **Die temperature**: The CPU temp shown on the web page is the chip's internal sensor, not ambient. Typical values are 30–50°C depending on activity. It's useful for confirming the underclock is working.

## Where Things Live

- Hardware diagrams, pin references, and power notes: `wiring.md`.
- Front vinyl, LED placement guide, and back label artwork for a 4-inch acrylic badge: `cricut/`.
- Common failure modes and recovery steps: `troubleshooting.md`.

## License / Credits

Personal project. Use at your own risk.
