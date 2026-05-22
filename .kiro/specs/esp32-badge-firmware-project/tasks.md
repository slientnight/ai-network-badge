# Implementation Plan: ESP32 Badge Firmware Project

## Overview

This plan converts the existing single-file ESP32-C3 badge firmware into an Arduino-IDE-friendly sketch project at `ai_network_badge/ai_network_badge.ino` and adds three Markdown docs at the workspace root. The work is a verbatim restructure: every function name, signature, control-flow path, literal value, NVS key, HTTP route, and BLE timing constant from `ai_network_badge_firmware_with_contacts.cpp` is preserved. There are no behavior changes.

Implementation language is C++ for Arduino-ESP32 (already established in the design and clarify phase). Per the design's Testing Strategy, this iteration uses static structural inspection only — no property-based or unit tests are added. Tasks are sequenced so the new sketch is fully written and verified before the original `.cpp` is removed, so behavior is never temporarily lost.

## Tasks

- [x] 1. Create the sketch folder and write the includes, CONFIG block, and hardware/limits sections
  - Create directory `ai_network_badge/` at the workspace root.
  - Create file `ai_network_badge/ai_network_badge.ino`.
  - Write Section 1 (Includes): every `#include` from the original `.cpp` in the original order — `WiFi.h`, `WebServer.h`, `DNSServer.h`, `Preferences.h`, `Adafruit_NeoPixel.h`, `BLEDevice.h`, `BLEUtils.h`, `BLEScan.h`, `BLEAdvertisedDevice.h`, `math.h`.
  - Write Section 2 (CONFIG block): banner-delimited block with the `===` start marker labeled `CONFIG — Edit values in this block to personalize the badge.` and matching `END CONFIG` end marker. Inside the block, include the "change before event" comment listing `BADGE_OWNER`, `BADGE_TITLE`, `LINKEDIN_URL`, `GITHUB_URL`, `BLE_BADGE_NAME`, `BLE_BADGE_PREFIX`, `ADMIN_KEY`. Define each of the seven constants as `const char*` with its original literal value and an inline comment above it explaining what it controls.
  - Write Section 3 (Hardware and limits): `#define LED_PIN 4`, `#define BOOT_BUTTON 9`, `#define NUM_LEDS 8`, `AP_SSID`, `AP_PASS`, `MAX_CONTACTS = 25`, `CONTACT_PACKET_VALUE = 3`, `BLE_SCAN_INTERVAL_MS = 20000`, `BLE_SCAN_SECONDS = 3`, `MAX_SEEN_PEERS = 12`, `REACTION_MS = 4500`, `DNS_PORT = 53`, `apIP(192,168,4,1)`, `netMsk(255,255,255,0)`. Match types and `const`-ness to the original.
  - _Requirements: 1.1, 1.2, 1.3, 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10, 2.11, 3.1, 3.2, 3.3, 4.1, 4.2, 4.3_

- [x] 2. Add globals, runtime state, and the Reaction enum
  - Append Section 4 (Globals and runtime state) to `ai_network_badge.ino`.
  - Declare `Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800)`, `WebServer server(80)`, `DNSServer dnsServer`, `Preferences prefs`.
  - Declare `brightness = 32`, `idlePattern = 0`, `packetCount = 0`, `contactPacketCount = 0`, `peerSeenCount = 0` with the same types as the original.
  - Declare `BLEScan* bleScan = nullptr`, `lastBleScan = 0`, `seenPeers[MAX_SEEN_PEERS]`, `seenPeerSlots = 0`, `frame = 0`, `lastFrame = 0`, `lastButtonCheck = 0`, `lastButtonState = HIGH`.
  - Define the `Reaction` enum with values `REACTION_NONE`, `REACTION_PACKET`, `REACTION_LINKUP`, `REACTION_AI`, `REACTION_STORM`, `REACTION_GITHUB`, `REACTION_LINKEDIN`, `REACTION_CONTACT` in this exact order.
  - Declare `activeReaction = REACTION_NONE`, `reactionStart = 0`, `lastSignal = "None yet"`.
  - _Requirements: 4.2, 6.1, 6.2, 7.1, 11.7_

- [x] 3. Add helper functions in dependency order
  - Append Section 5 (Helpers) to `ai_network_badge.ino`.
  - Copy verbatim, in this order: `idlePatternName`, `badgeLevelNameForCount`, `badgeLevelName`, `badgeLevelMeaning`, `nextUnlockCount`, `nextUnlockText`, `crossedUnlock`, `saveSettings`, `loadSettings`, `keyFor`, `wheel`, `clearPixels`, `cleanInput`, `escapeHtml`, `csvEscape`, `adminAllowed`, `peerAlreadySeen`, `rememberPeer`, `class BadgeAdvertisedDeviceCallbacks`, `startBlePresence`, `runBlePresenceScan`.
  - Preserve every literal: level thresholds `{0, 1, 5, 10, 25, 50}`, unlock thresholds `{1, 5, 10, 25, 50}`, NVS keys (`brightness`, `idlePattern`, `packetCount`, `contactCount`, `peerSeen`), `keyFor` `%s%02u` format, BLE scan setup values (`setInterval(160)`, `setWindow(80)`, `setMinPreferred(0x06)`, `setMaxPreferred(0x12)`).
  - Verify `rememberPeer` filters by `BLE_BADGE_PREFIX`, rejects `BLE_BADGE_NAME`, dedupes via `peerAlreadySeen`, ring-overwrites slot 0 once `MAX_SEEN_PEERS` is reached, increments `peerSeenCount`, sets `lastSignal`, calls `saveSettings`, and triggers `REACTION_LINKUP`.
  - _Requirements: 4.4, 4.5, 7.2, 7.3, 8.2, 8.3, 9.1, 10.1, 10.3, 10.4, 10.5, 10.6, 11.1, 11.2, 11.3, 11.4, 11.5, 11.6_

- [x] 4. Add idle pattern functions and the renderIdle dispatcher
  - Append Section 6 (Idle patterns) to `ai_network_badge.ino`.
  - Copy verbatim: `idlePacketChase`, `idleAIPulse`, `idleSparkle`, `idleRainbowMesh`, `renderIdle`.
  - Preserve all animation literals: chase head/tail colors `(0, 180, 255)` / `(0, 70, 130)` / `(0, 20, 45)`, AI pulse math `sin(frame * 0.10)`, sparkle decay step of 8 and `random(0, 5) == 0` spawn rate with color `(0, 255, 120)`, rainbow `wheel((i * 256 / NUM_LEDS + frame * 3) & 255)`.
  - Verify `renderIdle` falls back to `idlePattern = 0; idlePacketChase()` for unknown values.
  - _Requirements: 5.1, 5.2_

- [x] 5. Add reaction effect functions and the renderReaction dispatcher
  - Append Section 7 (Reaction effects) to `ai_network_badge.ino`.
  - Copy verbatim: `reactionPacket`, `reactionLinkUp`, `reactionAI`, `reactionStorm`, `reactionGithub`, `reactionLinkedIn`, `reactionContact`, `renderReaction`, `triggerReaction`.
  - Preserve all timing constants and color math: linkup `map(elapsed, 0, REACTION_MS, 0, NUM_LEDS + 1)` and color `(0, 220, 70)`, AI pulse `sin(elapsed * 0.015)`, storm `random(0, 3) == 0`, github cycle `((elapsed / 120 + i) % 3) == 0` with colors `(230,230,230)` and `(0,120,40)`, linkedin `sin(elapsed * 0.012)`, contact wave `(elapsed / 85) % (NUM_LEDS + 4)` with color math `(level, level/2, level/8)`, packet step `(elapsed / 90) % NUM_LEDS`.
  - Verify `renderReaction` returns to `REACTION_NONE` once `elapsed > REACTION_MS`.
  - Verify `triggerReaction` increments `packetCount` by `packetValue`, calls `crossedUnlock` and promotes to `REACTION_STORM` with `lastSignal = "LEVEL UP: " + badgeLevelName()` on threshold crossing, and always calls `saveSettings`.
  - _Requirements: 6.1, 6.2, 6.3, 6.4, 7.3_

- [ ] 6. Add HTML page builders
  - Append Section 8 (HTML page builders) to `ai_network_badge.ino`.
  - Copy verbatim: `htmlPage`, `contactSuccessPage`, `adminContactsPage`, `captivePortalLandingPage`.
  - Preserve every form action, button label, route reference (`/trigger?fx=packet|linkup|ai|storm|github|linkedin`, `/contact`, `/contacts.csv?key=`, `/clearcontacts?key=`, `/next`, `/brightness`, `/resetcount`), input `maxlength` values (32 / 80 / 120), and the `Cache-Control` / `Pragma` `<meta>` tags inside `captivePortalLandingPage`.
  - _Requirements: 3.4, 4.5, 5.5, 6.4, 7.4, 8.1, 8.3, 9.2, 9.3, 9.4_

- [ ] 7. Add HTTP handler functions
  - Append Section 9 (HTTP handlers) to `ai_network_badge.ino`.
  - Copy verbatim, in this order: `handleRoot`, `handleCaptivePortalProbe`, `redirectToPortal`, `handleTrigger`, `handleContactSubmit`, `handleContactsAdmin`, `handleContactsCsv`, `handleClearContacts`, `handleNext`, `handleBrightness`, `handleResetCount`.
  - Verify `handleCaptivePortalProbe` sends `Cache-Control: no-store, no-cache, must-revalidate, max-age=0` and `Pragma: no-cache`.
  - Verify `redirectToPortal` returns 302 with `Location: http://192.168.4.1`.
  - Verify `handleContactSubmit` runs each field through `cleanInput` with limits 32 / 80 / 120, returns 303 to `/` for empty submissions with `lastSignal = "Blank packet card ignored"`, returns 507 plain-text when `contactPacketCount >= MAX_CONTACTS`, persists `nm##` / `ct##` / `nt##` via `keyFor`, increments `contactPacketCount`, and calls `triggerReaction(REACTION_CONTACT, "Contact packet received", CONTACT_PACKET_VALUE)`.
  - Verify `handleContactsAdmin`, `handleContactsCsv`, `handleClearContacts` return 403 plain-text without a matching `?key=`. Verify `handleContactsCsv` sends `Content-Disposition: attachment; filename=badge_contacts.csv` and `text/csv`. Verify `handleClearContacts` removes all `nm##` / `ct##` / `nt##` keys, resets `contactPacketCount`, calls `saveSettings`, and 303-redirects to `/contacts?key=<ADMIN_KEY>`.
  - Verify `handleBrightness` clamps via `constrain(..., 5, 120)`. Verify `handleNext` advances `(idlePattern + 1) % 4`. Verify `handleResetCount` zeroes `packetCount`, `peerSeenCount`, and `seenPeerSlots`.
  - _Requirements: 3.4, 3.5, 3.6, 4.4, 4.5, 5.5, 6.4, 7.4, 8.1, 8.2, 8.3, 8.4, 8.5, 8.6, 8.7, 9.1, 9.2, 9.3, 9.4_

- [ ] 8. Add the BOOT button input handler
  - Append Section 10 (Input) to `ai_network_badge.ino`.
  - Copy `checkButton` verbatim.
  - Verify it uses a 30 ms debounce window, advances `(idlePattern + 1) % 4` only on the HIGH-to-LOW edge, sets `lastSignal = "BOOT button"`, calls `saveSettings`, and triggers `REACTION_LINKUP` with `reactionStart = millis()`.
  - _Requirements: 5.4_

- [ ] 9. Add setup() and loop() entry points
  - Append Section 11 (Entry points) to `ai_network_badge.ino`.
  - Copy `setup` verbatim: `Serial.begin(115200)`, 300 ms delay, `pinMode(BOOT_BUTTON, INPUT_PULLUP)`, `prefs.begin("badge", false)`, `loadSettings()`, NeoPixel `begin/setBrightness/clear/show`, `randomSeed(esp_random())`, `WiFi.mode(WIFI_AP)`, `WiFi.softAPConfig(apIP, apIP, netMsk)`, `WiFi.softAP(AP_SSID)`, `dnsServer.start(DNS_PORT, "*", apIP)`, `startBlePresence()`.
  - Register all routes verbatim, including the captive-probe set (`/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`, `/success.txt`, `/success.html`, `/blank.html`, `/ncsi.txt`, `/connecttest.txt`, `/redirect`) and `server.onNotFound(handleCaptivePortalProbe)`. Register `/contact` with `HTTP_POST`. Register all other routes as default (GET).
  - Call `server.begin()`, the Serial banner block, and `triggerReaction(REACTION_AI, "Badge booted", 0)` last.
  - Copy `loop` verbatim: `dnsServer.processNextRequest()`, `server.handleClient()`, `runBlePresenceScan()`, `checkButton()`, the 55 ms frame gate that updates `pixels.setBrightness(brightness)`, dispatches to `renderReaction` or `renderIdle`, calls `pixels.show()`, increments `frame`, and updates `lastFrame`. Match the original's tail (the `else` branch rendering idle, `pixels.show()`, `frame++`) — the original `.cpp` truncates mid-loop in the source we're working from, but the design specifies the full body; reconstruct it as: if `activeReaction != REACTION_NONE` call `renderReaction()` else call `renderIdle()`, then `pixels.show()` and `frame++`.
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 4.1, 4.2, 4.3, 5.2, 5.3, 5.5, 6.4, 7.4, 8.1, 9.1, 9.2, 9.3, 9.4, 10.1, 10.2, 11.1, 11.7_

- [ ] 10. Checkpoint - structural review of the .ino
  - Re-read `ai_network_badge/ai_network_badge.ino` end-to-end. Confirm the section order matches the design (Includes → CONFIG → Hardware/limits → Globals → Helpers → Idle patterns → Reaction effects → HTML builders → HTTP handlers → Input → setup/loop). Confirm every function appears before its first caller. Confirm the CONFIG banner markers and all seven CONFIG constants are present with their literal values and inline comments.
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 11. Write README.md per the documentation outline
  - Create `README.md` at the workspace root.
  - Include the sections from the design's README outline: project overview ("What It Is"), Supported Boards (Seeed XIAO ESP32-C3, ESP32-C3 SuperMini), Prerequisites (Arduino IDE 2.x or CLI, board manager URL `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`, board selection names `XIAO_ESP32C3` and `ESP32C3 Dev Module`, required libraries `Adafruit NeoPixel` and bundled ESP32 BLE), Project Layout, Configuration (Edit Before An Event) listing `BADGE_OWNER`, `BADGE_TITLE`, `LINKEDIN_URL`, `GITHUB_URL`, `BLE_BADGE_NAME`, `BLE_BADGE_PREFIX`, `ADMIN_KEY`, Build and Flash, First Boot Checklist (join `AI-BADGE`, open `http://192.168.4.1`, admin URL `http://192.168.4.1/contacts?key=<ADMIN_KEY>`, BOOT button cycles idle patterns), Where Things Live, License/Credits.
  - _Requirements: 12.1, 12.2, 12.3, 12.4, 12.5, 12.6, 12.7, 12.8_

- [ ] 12. Write wiring.md per the documentation outline
  - Create `wiring.md` at the workspace root.
  - Include sections from the design's wiring outline: Bill of Materials, LED Strip Wiring (GPIO 4 data, shared GND, 5V or 3.3V power, 8 pixels WS2812/SK6812, GRB + 800 kHz), BOOT Button (GPIO 9 built-in, no external wiring), Per-Board Pin Reference for Seeed XIAO ESP32-C3 and ESP32-C3 SuperMini (each calling out the GPIO 4 LED data pad, GND, and power pad), Power and Decoupling (470 µF cap across LED V+/GND, 330–470 Ω series resistor on data line), Sanity Check After Soldering.
  - _Requirements: 13.1, 13.2, 13.3, 13.4, 13.5, 13.6_

- [ ] 13. Write troubleshooting.md per the documentation outline
  - Create `troubleshooting.md` at the workspace root.
  - Include sections from the design's troubleshooting outline: Captive Portal Does Not Auto-Launch (with iOS, Android, Windows sub-sections), `AI-BADGE` Wi-Fi Network Not Visible, NeoPixels Not Lighting Up, BLE Peers Not Detected (referencing the 20 s / 3 s scan cadence and the `BLE_BADGE_PREFIX` filter), Contact Storage Full (referencing `/contacts.csv?key=` export and `/clearcontacts?key=` clear plus the `MAX_CONTACTS = 25` cap), Brightness Too Dim or Too Bright (referencing the 5–120 clamp), Sketch Will Not Compile (listing `Adafruit NeoPixel` and the bundled ESP32 BLE library), Upload Fails (USB driver requirement and ESP32-C3 SuperMini BOOT-held-while-plugging-in workaround).
  - _Requirements: 14.1, 14.2, 14.3, 14.4, 14.5, 14.6, 14.7, 14.8, 14.9_

- [ ] 14. Remove the original .cpp source file
  - Delete `ai_network_badge_firmware_with_contacts.cpp` from the workspace root.
  - This task runs only after tasks 1–13 are complete so the new sketch fully replaces it.
  - _Requirements: 1.4_

- [ ] 15. Final structural verification
  - Confirm `ai_network_badge/ai_network_badge.ino` exists and is the only `.ino`/`.h`/`.cpp` file inside `ai_network_badge/`.
  - Grep the `.ino` for the CONFIG banner start and end markers (`CONFIG — Edit values in this block to personalize the badge.` and `END CONFIG`).
  - Grep the `.ino` for each of the seven CONFIG constant names with their original literal values: `BADGE_OWNER` = `"Marshall"`, `BADGE_TITLE` = `"Network & Systems Architect"`, `LINKEDIN_URL` = `"https://www.linkedin.com/in/marshall-hollis"`, `GITHUB_URL` = `"https://github.com/slientnight"`, `BLE_BADGE_NAME` = `"AI-BADGE-MARSHALL"`, `BLE_BADGE_PREFIX` = `"AI-BADGE"`, `ADMIN_KEY` = `"meshadmin"`.
  - Grep the `.ino` for every route registration line in `setup()` (`/`, `/trigger`, `/contact` with `HTTP_POST`, `/contacts`, `/contacts.csv`, `/clearcontacts`, `/next`, `/brightness`, `/resetcount`, `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`, `/success.txt`, `/success.html`, `/blank.html`, `/ncsi.txt`, `/connecttest.txt`, `/redirect`, and `server.onNotFound`).
  - Confirm `README.md`, `wiring.md`, and `troubleshooting.md` exist at the workspace root and contain the section headings called out in tasks 11–13.
  - Confirm `ai_network_badge_firmware_with_contacts.cpp` no longer exists at the workspace root.
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- This iteration is a verbatim restructure: function names, signatures, NVS keys, HTTP routes, BLE timings, and animation literals are preserved exactly.
- Per the design's Testing Strategy, no automated tests (PBT or unit) are added in this iteration. Verification is structural inspection only.
- The original `.cpp` is removed only after the new `.ino` and the three doc files are in place, so behavior is never temporarily lost.
- Each task references specific requirements for traceability.
- Compilation is not verified by an ESP32 toolchain in this workflow — verification is structural and grep-based.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1"] },
    { "id": 1, "tasks": ["2"] },
    { "id": 2, "tasks": ["3"] },
    { "id": 3, "tasks": ["4", "5"] },
    { "id": 4, "tasks": ["6"] },
    { "id": 5, "tasks": ["7"] },
    { "id": 6, "tasks": ["8"] },
    { "id": 7, "tasks": ["9"] },
    { "id": 8, "tasks": ["11", "12", "13"] },
    { "id": 9, "tasks": ["14"] }
  ]
}
```
