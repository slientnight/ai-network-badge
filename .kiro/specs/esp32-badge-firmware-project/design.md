# Design Document

## Overview

This is a code-organization restructure, not a behavior change. Every runtime feature of `ai_network_badge_firmware_with_contacts.cpp` is preserved verbatim — function names, signatures, control flow, literal values, NVS keys, HTTP routes, BLE timings, animation math. The work is to (a) move the source into an Arduino-IDE-friendly sketch folder, (b) reorganize the single source file into a predictable top-to-bottom order with a clearly delimited `CONFIG` block, and (c) add three Markdown docs at the workspace root.

## Architecture

### File and Folder Layout

```
<workspace root>/
├── ai_network_badge/
│   └── ai_network_badge.ino      # The only source file. All firmware code lives here.
├── README.md                     # Project overview, supported boards, flashing.
├── wiring.md                     # Per-board pin reference, LED wiring, power notes.
├── troubleshooting.md            # Common failure modes and fixes.
└── .kiro/                        # (existing; spec artifacts)
```

Notes:
- `ai_network_badge/` is the Arduino sketch folder. Its name must match the `.ino` filename for the Arduino IDE to open it without renaming.
- No `.h`, `.cpp`, or other source files sit beside the `.ino`. Everything is in one translation unit, just like the original sketch.
- The original `ai_network_badge_firmware_with_contacts.cpp` at the workspace root is removed as part of the restructure.

### Sketch File Section Order

`ai_network_badge.ino` is organized into the following top-to-bottom sections, in order. Section banners (`// === Section Name ===`) are used between groups so the file reads as a guided walkthrough.

1. **Includes** — every `#include` from the original sketch, in the original order.
2. **CONFIG block** — all user-tunable identity constants, delimited by banner comments. See Data Models.
3. **Hardware and limits** — pin numbers, LED count, network constants that should not change between events: `LED_PIN`, `BOOT_BUTTON`, `NUM_LEDS`, `AP_SSID`, `AP_PASS`, `MAX_CONTACTS`, `CONTACT_PACKET_VALUE`, BLE timing constants, DNS port, AP IP literals.
4. **Globals and runtime state** — `Adafruit_NeoPixel pixels`, `WebServer server`, `DNSServer dnsServer`, `Preferences prefs`, brightness/idlePattern/counter variables, BLE scan state, the `seenPeers[]` ring, the `Reaction` enum, `activeReaction`, `reactionStart`, `lastSignal`, frame and button timing variables.
5. **Helpers** — small utilities and pure-ish functions (see Components and Interfaces for the order).
6. **Idle patterns** — `idlePacketChase`, `idleAIPulse`, `idleSparkle`, `idleRainbowMesh`, `renderIdle`.
7. **Reaction effects** — `reactionPacket`, `reactionLinkUp`, `reactionAI`, `reactionStorm`, `reactionGithub`, `reactionLinkedIn`, `reactionContact`, `renderReaction`, `triggerReaction`.
8. **HTML page builders** — `htmlPage`, `contactSuccessPage`, `adminContactsPage`, `captivePortalLandingPage`.
9. **HTTP handlers** — `handleRoot`, `handleCaptivePortalProbe`, `redirectToPortal`, `handleTrigger`, `handleContactSubmit`, `handleContactsAdmin`, `handleContactsCsv`, `handleClearContacts`, `handleNext`, `handleBrightness`, `handleResetCount`.
10. **Input** — `checkButton`.
11. **Entry points** — `setup()`, then `loop()`.

The order above is the same conceptual order the original sketch already follows; the restructure formalizes it with banner comments and ensures every helper is defined before its first use, which the original already does.

## Components and Interfaces

All components live in the single sketch file. The list below names each function group and the order of definitions inside the group; signatures and behavior are preserved verbatim from the original sketch.

### Helper functions (Section 5)

Defined in this exact order so every function appears before its first caller:

- `idlePatternName(uint8_t p) -> String`
- `badgeLevelNameForCount(uint32_t count) -> String`
- `badgeLevelName() -> String`
- `badgeLevelMeaning() -> String`
- `nextUnlockCount() -> uint32_t`
- `nextUnlockText() -> String`
- `crossedUnlock(uint32_t before, uint32_t after) -> bool`
- `saveSettings()`, `loadSettings()`
- `keyFor(const char* prefix, uint8_t index) -> String`
- `wheel(byte pos) -> uint32_t`, `clearPixels()`
- `cleanInput(String value, uint16_t maxLen) -> String`
- `escapeHtml(String value) -> String`, `csvEscape(String value) -> String`
- `adminAllowed() -> bool`
- `peerAlreadySeen(String peerName) -> bool`, `rememberPeer(String peerName)`
- `class BadgeAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks`
- `startBlePresence()`, `runBlePresenceScan()`

### Idle patterns (Section 6)

`idlePacketChase()`, `idleAIPulse()`, `idleSparkle()`, `idleRainbowMesh()`, then the dispatch wrapper `renderIdle()` that switches on `idlePattern` and falls back to `0` for unknown values.

### Reaction effects (Section 7)

Per-effect renderers `reactionPacket(elapsed)`, `reactionLinkUp(elapsed)`, `reactionAI(elapsed)`, `reactionStorm(elapsed)`, `reactionGithub(elapsed)`, `reactionLinkedIn(elapsed)`, `reactionContact(elapsed)`, then the dispatch wrapper `renderReaction()` and the entry point `triggerReaction(Reaction r, String signalName, uint8_t packetValue)` which also handles unlock detection and `REACTION_STORM` promotion.

### HTML page builders (Section 8)

`htmlPage()`, `contactSuccessPage(String name)`, `adminContactsPage()`, `captivePortalLandingPage()` — each returns a complete HTML document string.

### HTTP handlers and route table (Section 9)

Handler functions, in definition order: `handleRoot`, `handleCaptivePortalProbe`, `redirectToPortal`, `handleTrigger`, `handleContactSubmit`, `handleContactsAdmin`, `handleContactsCsv`, `handleClearContacts`, `handleNext`, `handleBrightness`, `handleResetCount`.

Route table installed in `setup()` (preserved exactly):

| Method | Path                          | Handler                     |
| ------ | ----------------------------- | --------------------------- |
| GET    | `/`                           | `handleRoot`                |
| GET    | `/trigger`                    | `handleTrigger`             |
| POST   | `/contact`                    | `handleContactSubmit`       |
| GET    | `/contacts`                   | `handleContactsAdmin`       |
| GET    | `/contacts.csv`               | `handleContactsCsv`         |
| GET    | `/clearcontacts`              | `handleClearContacts`       |
| GET    | `/next`                       | `handleNext`                |
| GET    | `/brightness`                 | `handleBrightness`          |
| GET    | `/resetcount`                 | `handleResetCount`          |
| GET    | `/generate_204`               | `handleCaptivePortalProbe`  |
| GET    | `/gen_204`                    | `handleCaptivePortalProbe`  |
| GET    | `/hotspot-detect.html`        | `handleCaptivePortalProbe`  |
| GET    | `/library/test/success.html`  | `handleCaptivePortalProbe`  |
| GET    | `/success.txt`                | `handleCaptivePortalProbe`  |
| GET    | `/success.html`               | `handleCaptivePortalProbe`  |
| GET    | `/blank.html`                 | `handleCaptivePortalProbe`  |
| GET    | `/ncsi.txt`                   | `handleCaptivePortalProbe`  |
| GET    | `/connecttest.txt`            | `handleCaptivePortalProbe`  |
| GET    | `/redirect`                   | `redirectToPortal`          |
| (any)  | (unmatched)                   | `handleCaptivePortalProbe`  |

### BLE callbacks

`BadgeAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice)` — extracts `getName()`, wraps in `String`, delegates to `rememberPeer`. Filtering and dedupe live in `rememberPeer`, not in the callback.

### Entry points

- `setup()` — `Serial.begin(115200)` → BOOT pinmode → `prefs.begin("badge")` → `loadSettings()` → NeoPixel init at saved brightness → `randomSeed(esp_random())` → Wi-Fi AP up at `192.168.4.1` → DNS server on port 53 → `startBlePresence()` → register all routes → `server.begin()` → log AP info to Serial → `triggerReaction(REACTION_AI, "Badge booted", 0)`.
- `loop()` — `dnsServer.processNextRequest()` → `server.handleClient()` → `runBlePresenceScan()` → `checkButton()` → frame loop on a 55 ms cadence (apply brightness, render reaction or idle, `pixels.show()`, `frame++`).

## Data Models

### CONFIG block layout

The CONFIG block sits immediately after the `#include` section and before any other code. It is delimited by clearly labeled banner comments and contains only the constants a non-developer might want to edit before an event.

Skeleton:

```cpp
// =============================================================================
// CONFIG — Edit values in this block to personalize the badge.
// Everything below this block is implementation and should not need editing.
// =============================================================================

// ---- Change these before taking the badge to an event ----------------------
//   BADGE_OWNER, BADGE_TITLE, LINKEDIN_URL, GITHUB_URL,
//   BLE_BADGE_NAME, BLE_BADGE_PREFIX, ADMIN_KEY
// ----------------------------------------------------------------------------

// Display name shown on the badge web page ("Hi, I'm <BADGE_OWNER>").
const char* BADGE_OWNER = "Marshall";

// Subtitle / role line shown under the owner name on the web page.
const char* BADGE_TITLE = "Network & Systems Architect";

// LinkedIn profile linked from the "Connect on LinkedIn" button.
const char* LINKEDIN_URL = "https://www.linkedin.com/in/marshall-hollis";

// GitHub profile linked from the "View GitHub" button.
const char* GITHUB_URL   = "https://github.com/slientnight";

// Local BLE advertising name. Other badges look for this when scanning peers.
const char* BLE_BADGE_NAME   = "AI-BADGE-MARSHALL";

// Prefix used to detect peer badges during BLE scans. Names starting with
// this prefix (and not equal to BLE_BADGE_NAME) are counted as peers.
const char* BLE_BADGE_PREFIX = "AI-BADGE";

// URL key required for /contacts, /contacts.csv, /clearcontacts admin pages.
// This is a soft gate, not real auth — change it before each event.
const char* ADMIN_KEY = "meshadmin";

// =============================================================================
// END CONFIG
// =============================================================================
```

Rules for the CONFIG block:
- Only the constants listed in Requirement 2 belong here. Hardware constants (`LED_PIN`, `NUM_LEDS`, `BOOT_BUTTON`), Wi-Fi AP constants (`AP_SSID`, `AP_PASS`), and limits (`MAX_CONTACTS`, `CONTACT_PACKET_VALUE`) live just below the CONFIG block in a separate "Hardware and limits" section so the CONFIG block stays focused on identity values an event-day editor would touch.
- Each constant has its own inline comment on the line above explaining what it controls.
- The "change before event" sub-section is a comment block at the top of CONFIG that names every constant that typically changes per event.
- The block is bracketed by `===` banner lines so it is visually unmistakable when scrolling.

### Hardware and limits (just below CONFIG)

Preserved verbatim from the original:

| Constant                  | Value                  | Purpose                                            |
| ------------------------- | ---------------------- | -------------------------------------------------- |
| `LED_PIN`                 | `4`                    | NeoPixel data pin                                  |
| `BOOT_BUTTON`             | `9`                    | Onboard BOOT button                                |
| `NUM_LEDS`                | `8`                    | NeoPixel count                                     |
| `AP_SSID`                 | `"AI-BADGE"`           | Wi-Fi SoftAP SSID                                  |
| `AP_PASS`                 | `""`                   | Open network                                       |
| `MAX_CONTACTS`            | `25`                   | NVS-backed contact form storage cap                |
| `CONTACT_PACKET_VALUE`    | `3`                    | Packets awarded per accepted contact submission    |
| `BLE_SCAN_INTERVAL_MS`    | `20000`                | Time between BLE scans                             |
| `BLE_SCAN_SECONDS`        | `3`                    | BLE scan duration                                  |
| `MAX_SEEN_PEERS`          | `12`                   | Max remembered peer names                          |
| `REACTION_MS`             | `4500`                 | Reaction effect duration                           |
| `DNS_PORT`                | `53`                   | DNS server UDP port                                |
| `apIP`, `netMsk`          | `192.168.4.1`, `/24`   | SoftAP IP and netmask                              |

### Runtime state

| Variable                                                | Type                  | Initial      | Notes                                              |
| ------------------------------------------------------- | --------------------- | ------------ | -------------------------------------------------- |
| `brightness`                                            | `uint8_t`             | `32`         | Clamped to `[5, 120]` on write; persisted          |
| `idlePattern`                                           | `uint8_t`             | `0`          | Cycled mod 4; persisted                            |
| `packetCount`                                           | `uint32_t`            | `0`          | Mesh game score; persisted                         |
| `contactPacketCount`                                    | `uint32_t`            | `0`          | Number of stored contacts; persisted               |
| `peerSeenCount`                                         | `uint32_t`            | `0`          | Lifetime distinct BLE peers; persisted             |
| `seenPeers[MAX_SEEN_PEERS]`, `seenPeerSlots`            | `String[12]`, `uint8_t` | empty / `0` | Volatile dedupe ring                               |
| `activeReaction`, `reactionStart`                       | `Reaction`, `unsigned long` | `NONE`, `0` | Volatile reaction state                            |
| `frame`, `lastFrame`, `lastButtonCheck`, `lastButtonState` | counters / bool       | `0` / `HIGH` | Volatile loop timing                               |
| `lastSignal`                                            | `String`              | `"None yet"` | Volatile, displayed on web page                    |

### NVS schema (namespace `badge`)

| Key            | Type     | Source variable        |
| -------------- | -------- | ---------------------- |
| `brightness`   | `uchar`  | `brightness`           |
| `idlePattern`  | `uchar`  | `idlePattern`          |
| `packetCount`  | `uint`   | `packetCount`          |
| `contactCount` | `uint`   | `contactPacketCount`   |
| `peerSeen`     | `uint`   | `peerSeenCount`        |
| `nm00`..`nm24` | `String` | Contact name           |
| `ct00`..`ct24` | `String` | Contact contact field  |
| `nt00`..`nt24` | `String` | Contact note           |

The `nm##`/`ct##`/`nt##` keys are produced by `keyFor("nm" / "ct" / "nt", index)` with the index zero-padded to two digits.

### Reaction enum

```cpp
enum Reaction {
  REACTION_NONE,
  REACTION_PACKET,
  REACTION_LINKUP,
  REACTION_AI,
  REACTION_STORM,
  REACTION_GITHUB,
  REACTION_LINKEDIN,
  REACTION_CONTACT
};
```

## Error Handling

Inherited verbatim from the original sketch — the restructure does not change any error path.

- **Admin gate failures** — `/contacts`, `/contacts.csv`, `/clearcontacts` without a matching `?key=<ADMIN_KEY>` respond `403` plain-text with a hint to add the query parameter.
- **Blank contact form** — empty after sanitization sets `lastSignal = "Blank packet card ignored"` and `303`-redirects to `/`. No NVS write.
- **Contact storage full** — when `contactPacketCount >= MAX_CONTACTS`, responds `507` plain-text with a "storage full" message; no submission is recorded.
- **Captive portal probes** — every Apple/Android/Windows captive-detection URL and any unregistered path returns the captive landing page with `Cache-Control: no-store, no-cache, must-revalidate, max-age=0` and `Pragma: no-cache`, so devices treat the network as captive and auto-launch the page.
- **Unknown idle pattern** — `renderIdle` falls back to pattern `0` (Packet Chase) if `idlePattern` is out of range.
- **BLE scan not initialized** — `runBlePresenceScan` returns early if `bleScan == nullptr`.
- **Brightness out of range** — clamped to `[5, 120]` via `constrain` before write.
- **Sanitization** — `cleanInput` trims, replaces `\r`/`\n` with spaces, collapses double spaces, and truncates to `maxLen` before any HTML/CSV/storage use.

## Testing Strategy

Because this iteration is a verbatim reorganization of an Arduino sketch with no host-compilable test harness, automated tests are not added in this fast-task. Verification relies on:

- **Static structural checks** for file layout, presence/absence of files, the CONFIG block's banner markers, the seven CONFIG constants and their literal values, the inline comments per constant, the "change before event" comment block, and the registration of every HTTP route in `setup()`.
- **Static doc checks** for required headings and string fragments in `README.md`, `wiring.md`, and `troubleshooting.md`.
- **Manual on-device verification** for runtime behavior: AP comes up, captive portal auto-launches on iOS/Android/Windows, NeoPixels animate, BOOT button cycles idle patterns, `/trigger` reactions fire, contact form persists across reboot, BLE peer discovery records peers, admin endpoints gate on `?key=`.

Identified property-worthy contracts that the preserved code already satisfies (see Correctness Properties below) are documented as contracts the implementation must continue to honor. They are not implemented as automated tests in this iteration; doing so would require extracting the helpers into a host-compilable target, which is out of scope for the restructure.

## Acceptance Criteria Testing Prework

See the prework tool output for the per-criterion classification. Summary:

- **SMOKE / EXAMPLE checks** dominate. Most criteria are static structural checks on file presence, file content, constant values, header/route registration, or doc section presence. They are best verified by static inspection and simple grep-based assertions, not by property-based tests.
- **PROPERTY candidates** identified in the existing helpers:
  - `badgeLevelNameForCount(count)` → highest threshold ≤ count maps to the corresponding level name (Requirement 7.2).
  - `crossedUnlock(before, after)` → true iff some unlock threshold lies in the half-open interval `(before, after]` (Requirement 7.3).
  - `cleanInput(value, maxLen)` → output length ≤ maxLen, no leading/trailing whitespace, no consecutive spaces, no `\r` or `\n` (Requirements 8.2, 8.3).
  - `rememberPeer(name)` → records iff `name` starts with `BLE_BADGE_PREFIX`, is not equal to `BLE_BADGE_NAME`, and is not already seen; the seen list never exceeds `MAX_SEEN_PEERS` (Requirements 10.3, 10.4).
  - `saveSettings` / `loadSettings` round-trip (Requirements 11.2–11.6).

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system — essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Level name reflects the highest reached threshold

*For any* non-negative integer `count`, `badgeLevelNameForCount(count)` returns the level name corresponding to the highest threshold in `{0, 1, 5, 10, 25, 50}` that is less than or equal to `count`: `"Supernode"` when `count >= 50`, `"AI Router"` when `25 <= count < 50`, `"Mesh Builder"` when `10 <= count < 25`, `"Linked Node"` when `5 <= count < 10`, `"Listening Node"` when `1 <= count < 5`, and `"Offline Node"` when `count == 0`.

**Validates: Requirements 7.2**

### Property 2: Unlock crossing detection

*For any* pair of non-negative integers `(before, after)` with `before <= after`, `crossedUnlock(before, after)` returns `true` if and only if at least one threshold in `{1, 5, 10, 25, 50}` lies in the half-open interval `(before, after]`.

**Validates: Requirements 7.3**

### Property 3: Input sanitization preserves bounds and removes structural whitespace

*For any* string `value` and any non-negative `maxLen`, the output of `cleanInput(value, maxLen)` (a) has length less than or equal to `maxLen`, (b) does not start or end with a whitespace character when non-empty, (c) contains no two consecutive space characters, and (d) contains no carriage return or newline characters.

**Validates: Requirements 8.2, 8.3**

### Property 4: BLE peer recording is filtered, deduped, and capped

*For any* sequence of advertised peer names processed by `rememberPeer`, after processing: (a) only names that start with `BLE_BADGE_PREFIX` and are not equal to `BLE_BADGE_NAME` ever appear in the seen-peer list, (b) no name appears more than once in the seen-peer list within the same retention window, and (c) the number of stored peer slots never exceeds `MAX_SEEN_PEERS`.

**Validates: Requirements 10.3, 10.4**

### Property 5: Persistent settings round-trip through NVS

*For any* tuple of in-memory settings `(brightness, idlePattern, packetCount, contactPacketCount, peerSeenCount)` that has been written via `saveSettings`, a subsequent `loadSettings` call (against the same NVS namespace `badge`) restores the same values into the in-memory variables, with no mutation or truncation.

**Validates: Requirements 11.2, 11.3, 11.4, 11.5, 11.6**

## Documentation Outlines

### `README.md`

```
# AI Network Badge

## What It Is
- Short pitch: open Wi-Fi, captive portal, NeoPixels, BLE peer discovery, contact card form.
- Screenshot or feature list.

## Supported Boards
- Seeed XIAO ESP32-C3
- ESP32-C3 SuperMini

## Prerequisites
- Arduino IDE 2.x or Arduino CLI
- Board manager URL: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
- Board selection name (per board)
  - Seeed XIAO ESP32-C3: "XIAO_ESP32C3"
  - ESP32-C3 SuperMini: "ESP32C3 Dev Module"
- Required libraries
  - Adafruit NeoPixel
  - ESP32 BLE (bundled with the espressif/arduino-esp32 core)

## Project Layout
- ai_network_badge/ai_network_badge.ino — single-file sketch
- README.md, wiring.md, troubleshooting.md — documentation

## Configuration (Edit Before An Event)
- Pointer to the CONFIG block in the .ino
- List of constants to update (BADGE_OWNER, BADGE_TITLE, LINKEDIN_URL, GITHUB_URL, BLE_BADGE_NAME, BLE_BADGE_PREFIX, ADMIN_KEY)

## Build and Flash
- Arduino IDE: open ai_network_badge/ai_network_badge.ino, select board, upload
- Arduino CLI: example commands for compile and upload, per board

## First Boot Checklist
- Join Wi-Fi network "AI-BADGE" (open, no password)
- Captive portal should auto-launch; if not, open http://192.168.4.1
- Admin contacts page: http://192.168.4.1/contacts?key=<ADMIN_KEY>
- Verify NeoPixels animate and BOOT button cycles idle patterns

## Where Things Live
- One-line pointer to wiring.md for hardware
- One-line pointer to troubleshooting.md for failures

## License / Credits
- Optional placeholder
```

### `wiring.md`

```
# Wiring Guide

## Bill of Materials
- 1x ESP32-C3 board (XIAO ESP32-C3 or ESP32-C3 SuperMini)
- 1x 8-pixel WS2812 / SK6812 NeoPixel strip or ring
- USB-C cable
- Optional: 470 uF capacitor across LED V+ / GND, 330-470 ohm resistor in series with data line

## LED Strip Wiring (Both Boards)
- Data: GPIO 4 -> strip DIN
- Ground: board GND -> strip GND
- Power: board 5V (or 3.3V on smaller strips) -> strip V+
- 8 pixels, WS2812 or SK6812, GRB color order, 800 kHz timing

## BOOT Button
- GPIO 9, built into both supported boards
- No external wiring required
- Pulled HIGH internally; pulses LOW when pressed

## Per-Board Pin Reference

### Seeed XIAO ESP32-C3
- LED data: GPIO 4 pad
- GND: GND pad
- Power: 5V or 3V3 pad
- Pin diagram or photo placeholder

### ESP32-C3 SuperMini
- LED data: GPIO 4 pin
- GND: GND pin
- Power: 5V or 3V3 pin
- Pin diagram or photo placeholder

## Power and Decoupling
- 470 uF electrolytic across LED V+ and GND, near the strip
- 330-470 ohm series resistor on the data line, close to the first pixel
- Keep data wire short to reduce noise

## Sanity Check After Soldering
- USB power only, no battery
- Expect a brief boot animation, then the default Packet Chase idle pattern
```

### `troubleshooting.md`

```
# Troubleshooting

## Captive Portal Does Not Auto-Launch
- iOS: open Safari, navigate to http://192.168.4.1, or toggle Wi-Fi off/on
- Android: tap the "Sign in to network" notification, or open Chrome to http://192.168.4.1
- Windows: click the Wi-Fi icon -> "Open browser" link, or open Edge to http://192.168.4.1

## "AI-BADGE" Wi-Fi Network Not Visible
- Confirm the badge is powered (any LED activity)
- Move closer; ESP32-C3 AP range is short
- Forget any cached "AI-BADGE" entry on the phone and rescan
- Reboot the badge

## NeoPixels Not Lighting Up
- Verify GPIO 4 -> DIN, not DOUT
- Verify ground is shared between board and strip
- Check strip orientation (data flows one direction)
- Try a lower brightness via /brightness if the strip flashes white then dies (power issue)

## BLE Peers Not Detected
- Both badges must be powered and within ~5 m
- Peer's BLE name must start with the prefix in BLE_BADGE_PREFIX
- Scans run every 20 s for 3 s; wait at least one full cycle
- Reboot a badge if its BLE stack got into a bad state

## Contact Storage Full
- Admin export: http://192.168.4.1/contacts.csv?key=<ADMIN_KEY>
- Admin clear: http://192.168.4.1/clearcontacts?key=<ADMIN_KEY>
- Storage cap is 25 contacts (MAX_CONTACTS)

## Brightness Too Dim or Too Bright
- /brightness clamps the value to the inclusive range 5-120
- Default is 32; raise carefully to avoid pulling more current than USB supplies

## Sketch Will Not Compile
- Install the Adafruit NeoPixel library via Library Manager
- Install the ESP32 board package (BLE library is bundled with it)
- Confirm board selection matches the connected hardware

## Upload Fails
- Install the USB-serial driver for your board (CP210x or CH340 depending on revision)
- ESP32-C3 SuperMini: hold the BOOT button while plugging in to enter download mode, then release after the IDE starts upload
- Try a different USB cable; some are charge-only
```

## Out of Scope

- Refactoring helpers into separate translation units or a host-compilable test harness.
- Adding new behavior, new endpoints, or new animations.
- Changing any constant value other than the documented CONFIG values during the restructure (those keep their original values too).
- Implementing automated tests for the correctness properties — they are documented contracts only in this iteration.
