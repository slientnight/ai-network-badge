# Implementation Plan: Badge Discovery and Security Upgrades

## Overview

This plan modifies the existing single-file Arduino sketch at `firmware/firmware.ino` to add three layered upgrades: a per-badge admin key, a Nearby Badges peer record list, and an in-RAM Recent Activity ring buffer. Every change lands in one of the existing numbered sections (1–11) of the sketch. Tasks are ordered so each step compiles cleanly on top of the previous one: types and globals first, then helpers (in dependency order, each before its first caller), then the call-site changes that consume them, and finally the entry-point wiring.

There is no host-compilable test harness for the sketch (per the design's Testing Strategy), so verification sub-tasks marked with `*` are structural `grep` inspections that confirm the documented choke points and renamings landed correctly. Property-based tests are documented in the design as future work; the structural checks here serve as their stand-ins.

Convert the feature design into a series of prompts for a code-generation LLM that will implement each step with incremental progress. Make sure that each prompt builds on the previous prompts, and ends with wiring things together. There should be no hanging or orphaned code that isn't integrated into a previous step. Focus ONLY on tasks that involve writing, modifying, or testing code.

## Tasks

- [ ] 1. Add new constants, types, and globals to Sections 3 and 4
  - [ ] 1.1 Add new size constants `ACTIVITY_BUFFER_SIZE = 8` and `SERIAL_LINE_MAX = 96` to Section 3 (next to the existing `MAX_SEEN_PEERS` / `REACTION_MS` block)
    - File: `firmware/firmware.ino`, Section 3 ("Hardware and limits")
    - _Requirements: 9.3, 3.1_
  - [ ] 1.2 Add the `PeerRecord` struct, `ActivityCategory` enum (`ACTIVITY_REACTION`, `ACTIVITY_CONTACT`, `ACTIVITY_PEER`, `ACTIVITY_LEVEL_UP`), and `ActivityEntry` struct to Section 4, before the existing globals that will use them
    - File: `firmware/firmware.ino`, Section 4 ("Globals and runtime state")
    - _Requirements: 6.1, 9.1, 9.2_
  - [ ] 1.3 Replace `String seenPeers[MAX_SEEN_PEERS]` with `PeerRecord seenPeers[MAX_SEEN_PEERS]` in Section 4. Keep `seenPeerSlots` unchanged
    - _Requirements: 6.1, 6.4_
  - [ ] 1.4 Add new globals to Section 4: `String activeAdminKey = ""`, `ActivityEntry activityBuffer[ACTIVITY_BUFFER_SIZE]`, `uint8_t activityHead = 0`, `uint8_t activityCount = 0`, `String serialLine = ""`
    - _Requirements: 2.1, 2.2, 9.3, 3.1_
  - [ ]* 1.5 Verify type/global declarations landed
    - `grep -n "struct PeerRecord" firmware/firmware.ino` → 1 hit
    - `grep -n "enum ActivityCategory" firmware/firmware.ino` → 1 hit
    - `grep -n "struct ActivityEntry" firmware/firmware.ino` → 1 hit
    - `grep -n "PeerRecord seenPeers\[MAX_SEEN_PEERS\]" firmware/firmware.ino` → 1 hit
    - `grep -nc "String seenPeers\[" firmware/firmware.ino` → 0 (the old `String[]` declaration is gone)
    - `grep -n "activeAdminKey" firmware/firmware.ino` → declaration line present
    - `grep -n "activityBuffer\[ACTIVITY_BUFFER_SIZE\]" firmware/firmware.ino` → 1 hit
    - `grep -n "ACTIVITY_BUFFER_SIZE" firmware/firmware.ino` → constant defined
    - `grep -n "SERIAL_LINE_MAX" firmware/firmware.ino` → constant defined
    - _Requirements: 6.1, 9.1, 9.2, 9.3_

- [ ] 2. Remove the `ADMIN_KEY` constant from the CONFIG block
  - File: `firmware/firmware.ino`, CONFIG block above Section 3
  - [ ] 2.1 Delete the `const char* ADMIN_KEY = "meshadmin";` line and its preceding comment block reference; remove `ADMIN_KEY` from the CONFIG header comment listing the personalization fields. At this point the file will not compile until task 3 lands `activeAdminKey` references (expected — keep tasks sequential).
    - _Requirements: 1.1, 1.4_
  - [ ]* 2.2 Verify the constant and literal are gone
    - `grep -n "meshadmin" firmware/firmware.ino` → 0 hits
    - `grep -nE "(const char\\*|const char \\*)\\s*ADMIN_KEY" firmware/firmware.ino` → 0 hits
    - _Requirements: 1.1, 1.4_

- [ ] 3. Add new helpers to Section 5 in dependency order
  - File: `firmware/firmware.ino`, Section 5 ("Helpers"). Place each helper before its first caller. The order below matches the dependency chain documented in the design.
  - [ ] 3.1 Add `String macDerivedAdminKey()` — pure helper that reads the chip MAC (via `WiFi.macAddress()` or `esp_efuse_mac_get_default`) and formats the last 4 bytes as exactly 8 lowercase hex characters using `snprintf("%02x%02x%02x%02x", ...)`
    - _Requirements: 1.2_
  - [ ] 3.2 Add `String loadActiveAdminKey()` — reads `prefs.getString("adminKey", "")`; returns it when non-empty, otherwise returns `macDerivedAdminKey()`
    - _Requirements: 1.3, 2.1_
  - [ ] 3.3 Add `String relativeTimeString(unsigned long pastMs)` — implements the bucket table from Requirement 8 (`< 10s` → `"just now"`, `[10s, 60s)` → `"<n> sec ago"`, `[60s, 60min)` → `"<n> min ago"`, `[60min, 24h)` → `"<n> hr ago"`, `>= 24h` → `"24+ hr ago"`). Uses `unsigned long` subtraction; returns `"just now"` when `pastMs > millis()`
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 8.6, 8.7_
  - [ ] 3.4 Add `String rssiLabel(int8_t rssi)` — returns `"Near"` when `rssi >= -60`, `"Far"` when `-80 <= rssi < -60`, `"Distant"` when `rssi < -80`
    - _Requirements: 7.3, 7.4, 7.5_
  - [ ] 3.5 Add `void addActivity(ActivityCategory category, String label)` — single insertion choke point. Writes `{label, millis(), category}` to `activityBuffer[activityHead]`, advances `activityHead = (activityHead + 1) % ACTIVITY_BUFFER_SIZE`, increments `activityCount` capped at `ACTIVITY_BUFFER_SIZE`. No NVS writes.
    - _Requirements: 9.4, 9.5, 9.6_
  - [ ] 3.6 Add `String reactionName(Reaction r)` — small mapper from `Reaction` enum values to human labels (e.g. `REACTION_PACKET → "Packet"`, `REACTION_LINKUP → "Link"`, `REACTION_AI → "AI inference"`, `REACTION_STORM → "Storm"`, `REACTION_GITHUB → "GitHub"`, `REACTION_LINKEDIN → "LinkedIn"`, `REACTION_CONTACT → "Contact"`)
    - _Requirements: 10.1_
  - [ ] 3.7 Add `void processSerialLine(String line)` — trims whitespace and CR/LF; if `line.startsWith("setkey=")` extracts the trimmed value, persists to `prefs.putString("adminKey", value)`, updates `activeAdminKey`, and prints confirmation; if the value is empty after trim, prints an error and leaves state unchanged; if `line == "clearkey"`, removes the NVS key, sets `activeAdminKey = macDerivedAdminKey()`, and prints confirmation; otherwise ignores silently
    - _Requirements: 3.1, 3.2, 3.3, 3.4_
  - [ ] 3.8 Update `adminAllowed()` body to `return server.hasArg("key") && server.arg("key") == activeAdminKey;`
    - _Requirements: 5.1_
  - [ ]* 3.9 Verify helpers and the changed `adminAllowed`
    - `grep -n "String macDerivedAdminKey()" firmware/firmware.ino` → 1 hit
    - `grep -n "String loadActiveAdminKey()" firmware/firmware.ino` → 1 hit
    - `grep -n "String relativeTimeString(" firmware/firmware.ino` → 1 hit
    - `grep -n "String rssiLabel(" firmware/firmware.ino` → 1 hit
    - `grep -n "void addActivity(" firmware/firmware.ino` → 1 hit
    - `grep -n "String reactionName(" firmware/firmware.ino` → 1 hit
    - `grep -n "void processSerialLine(" firmware/firmware.ino` → 1 hit
    - `grep -n "server.arg(\"key\") == activeAdminKey" firmware/firmware.ino` → 1 hit (inside `adminAllowed`)
    - `grep -n "ADMIN_KEY" firmware/firmware.ino` → 0 hits
    - **Validates: Property 1 (RSSI buckets), Property 2 (relative-time buckets), Property 3 (activity buffer invariants), Property 4 (MAC-derived key format)**
    - _Requirements: 1.2, 1.3, 1.4, 5.1, 7.3, 7.4, 7.5, 8.1, 8.2, 8.3, 8.4, 8.5, 8.6, 8.7, 9.4, 9.5_

- [ ] 4. Convert `peerAlreadySeen` and `rememberPeer` to use `PeerRecord` and emit peer activity
  - File: `firmware/firmware.ino`, Section 5
  - [ ] 4.1 Update `peerAlreadySeen(String peerName)` to walk `seenPeers[i].name == peerName` instead of comparing the old `String` slot
    - _Requirements: 6.2, 6.3_
  - [ ] 4.2 Change `rememberPeer` signature from `void rememberPeer(String peerName)` to `void rememberPeer(String peerName, int8_t rssi)`. Preserve all existing prefix/self-name filters and the slot-0 overwrite-on-full behavior verbatim.
    - _Requirements: 6.1, 6.5, 12.4_
  - [ ] 4.3 In the first-sighting branch of `rememberPeer`, populate the new `PeerRecord` fully: `name = peerName`, `rssiFirst = rssiLast = rssi`, `firstSeenMs = lastSeenMs = millis()`. Keep existing side effects (`peerSeenCount++`, `lastSignal = ...`, `saveSettings()`, `activeReaction = REACTION_LINKUP`, `reactionStart = millis()`). After those, call `addActivity(ACTIVITY_PEER, "Peer: " + peerName)`. The slot-0 overwrite branch must also fully populate a fresh `PeerRecord` (matching the existing ring-overwrite semantics).
    - _Requirements: 6.2, 6.5, 10.4_
  - [ ] 4.4 Add a new repeat-sighting branch (early in the function) for when `peerAlreadySeen(peerName)` is true: locate the matching `PeerRecord`, set `rssiLast = rssi` and `lastSeenMs = millis()`, and return without touching `rssiFirst`, `firstSeenMs`, `peerSeenCount`, `lastSignal`, the reaction state, NVS, or the activity buffer
    - _Requirements: 6.3, 10.5_
  - [ ] 4.5 Update `BadgeAdvertisedDeviceCallbacks::onResult` to pass `(int8_t)advertisedDevice.getRSSI()` as the second argument to `rememberPeer`
    - _Requirements: 6.2, 12.4_
  - [ ]* 4.6 Verify peer plumbing
    - `grep -n "void rememberPeer(String peerName, int8_t rssi)" firmware/firmware.ino` → 1 hit
    - `grep -n "rememberPeer(peerName, " firmware/firmware.ino` → 1 hit (inside `onResult`)
    - `grep -n "advertisedDevice.getRSSI()" firmware/firmware.ino` → 1 hit (inside `onResult`)
    - `grep -n "addActivity(ACTIVITY_PEER" firmware/firmware.ino` → exactly 1 hit (only on the first-sighting branch of `rememberPeer`)
    - `grep -n "seenPeers\[.*\]\\.name" firmware/firmware.ino` → present (used by `peerAlreadySeen` and the repeat-sighting update)
    - **Validates: Property 5 (activity choke-point invariants — peer branch)**
    - _Requirements: 6.1, 6.2, 6.3, 6.5, 10.4, 10.5_

- [ ] 5. Update `triggerReaction` in Section 7 to emit reaction and level-up activity entries
  - File: `firmware/firmware.ino`, Section 7 ("Reaction effects")
  - [ ] 5.1 After the existing `activeReaction = r; reactionStart = millis(); lastSignal = signalName;` block, insert `if (r != REACTION_NONE) { addActivity(ACTIVITY_REACTION, reactionName(r)); }`
    - _Requirements: 10.1_
  - [ ] 5.2 Inside the existing `if (crossedUnlock(before, packetCount))` branch, after the existing storm-promotion lines (`activeReaction = REACTION_STORM; reactionStart = millis(); lastSignal = "LEVEL UP: " + badgeLevelName();`) and before `saveSettings()`, insert `addActivity(ACTIVITY_LEVEL_UP, "Level up: " + badgeLevelName());`. Both REACTION and LEVEL_UP entries can fire for the same call when applicable, in that order.
    - _Requirements: 10.2_
  - [ ]* 5.3 Verify activity calls inside `triggerReaction`
    - `grep -n "addActivity(ACTIVITY_REACTION" firmware/firmware.ino` → exactly 1 hit (inside `triggerReaction`)
    - `grep -n "addActivity(ACTIVITY_LEVEL_UP" firmware/firmware.ino` → exactly 1 hit (inside the `crossedUnlock` branch of `triggerReaction`)
    - **Validates: Property 5 (activity choke-point invariants — reaction and level-up branches)**
    - _Requirements: 10.1, 10.2_

- [ ] 6. Add Nearby Badges and Recent Activity sections to `htmlPage` in Section 8
  - File: `firmware/firmware.ino`, Section 8 ("HTML page builders"), inside `htmlPage()`
  - Both new sections live inside the existing `.card` container, between the `</div>` that closes "Connect" buttons and the `<h2>Send a packet</h2>` line. Reuse the existing `.game`, `.pill`, and `.small` styles — no new CSS.
  - [ ] 6.1 Insert the "Nearby Badges" section
    - Heading `<h2>Nearby Badges</h2>` followed by a `<div class='game'>` block
    - When `seenPeerSlots == 0`, render `<p class='small'>No peers seen yet.</p>`
    - Otherwise, iterate `for (uint8_t i = 0; i < seenPeerSlots; i++)` and for each populated `PeerRecord` render a `<p>` containing: `<b>` + `escapeHtml(seenPeers[i].name)` + `</b>`, a `<span class='pill'>` with `rssiLabel(seenPeers[i].rssiLast) + " (" + String(seenPeers[i].rssiLast) + " dBm)"`, and a `<span class='small'>` with `relativeTimeString(seenPeers[i].firstSeenMs)`
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7_
  - [ ] 6.2 Insert the "Recent Activity" section immediately after Nearby Badges
    - Heading `<h2>Recent Activity</h2>` followed by a `<div class='game'>` block
    - When `activityCount == 0`, render `<p class='small'>No activity yet.</p>`
    - Otherwise iterate newest-first using `for (uint8_t i = 0; i < activityCount; i++) { uint8_t idx = (activityHead + ACTIVITY_BUFFER_SIZE - 1 - i) % ACTIVITY_BUFFER_SIZE; ... }` and render each entry as a `<p>` with `escapeHtml(activityBuffer[idx].label)` followed by a `<span class='small'>` containing `relativeTimeString(activityBuffer[idx].timestamp)`
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5_
  - [ ] 6.3 Confirm the existing `lastSignal` pill inside the BUILD THE MESH `.game` block remains unchanged so the quick-glance summary is preserved
    - _Requirements: 11.6_
  - [ ]* 6.4 Verify the new HTML sections
    - `grep -n "<h2>Nearby Badges</h2>" firmware/firmware.ino` → 1 hit (inside `htmlPage`)
    - `grep -n "<h2>Recent Activity</h2>" firmware/firmware.ino` → 1 hit (inside `htmlPage`)
    - `grep -n "rssiLabel(" firmware/firmware.ino` → at least 1 hit inside `htmlPage`
    - `grep -n "relativeTimeString(" firmware/firmware.ino` → at least 2 hits inside `htmlPage` (one per new section)
    - `grep -n "(activityHead + ACTIVITY_BUFFER_SIZE - 1" firmware/firmware.ino` → 1 hit (newest-first iterator)
    - `grep -n "lastSignal" firmware/firmware.ino` → BUILD THE MESH pill line still present
    - _Requirements: 7.1, 7.2, 7.6, 7.7, 11.1, 11.2, 11.3, 11.4, 11.5, 11.6_

- [ ] 7. Update `adminContactsPage` in Section 8 to use `activeAdminKey`
  - File: `firmware/firmware.ino`, Section 8, inside `adminContactsPage()`
  - [ ] 7.1 Replace `String(ADMIN_KEY)` in both the `Download CSV` and `Clear contacts` `<a href='...'>` lines with `activeAdminKey`. The surrounding `?key=` query argument string is unchanged.
    - _Requirements: 5.4_
  - [ ]* 7.2 Verify the substitution
    - `grep -n "String(ADMIN_KEY)" firmware/firmware.ino` → 0 hits
    - `grep -n "/contacts.csv?key='\\s*+\\s*activeAdminKey" firmware/firmware.ino` → 1 hit
    - `grep -n "/clearcontacts?key='\\s*+\\s*activeAdminKey" firmware/firmware.ino` → 1 hit
    - _Requirements: 5.4_

- [ ] 8. Update contact and clear-contacts handlers in Section 9
  - File: `firmware/firmware.ino`, Section 9 ("HTTP handlers")
  - [ ] 8.1 In `handleContactSubmit`, after the existing `contactPacketCount++; triggerReaction(REACTION_CONTACT, "Contact packet received", CONTACT_PACKET_VALUE);` lines and before the `server.send(200, ...)` response, append `addActivity(ACTIVITY_CONTACT, name.length() > 0 ? "Contact from " + name : "Contact card received");`. Order matters: `triggerReaction` runs first so the REACTION entry is older than the CONTACT entry in the buffer.
    - _Requirements: 10.3_
  - [ ] 8.2 In `handleClearContacts`, change the redirect line `server.sendHeader("Location", String("/contacts?key=") + ADMIN_KEY);` to `server.sendHeader("Location", String("/contacts?key=") + activeAdminKey);`
    - _Requirements: 5.2, 5.4_
  - [ ]* 8.3 Verify the contact and clear handler changes
    - `grep -n "addActivity(ACTIVITY_CONTACT" firmware/firmware.ino` → exactly 1 hit (inside `handleContactSubmit`)
    - `grep -n "String(\"/contacts?key=\") + activeAdminKey" firmware/firmware.ino` → 1 hit (inside `handleClearContacts`)
    - **Validates: Property 5 (activity choke-point invariants — contact branch)**
    - _Requirements: 5.2, 5.4, 10.3_

- [ ] 9. Add `handleAdminKeyReveal` HTTP handler in Section 9
  - File: `firmware/firmware.ino`, Section 9, alongside the other handlers (place after `handleClearContacts` for readability)
  - [ ] 9.1 Define `void handleAdminKeyReveal()` with body: `if (digitalRead(BOOT_BUTTON) == LOW) { server.send(200, "text/plain", activeAdminKey); } else { server.send(403, "text/plain", "Hold the BOOT button on the badge and reload this page."); }`. Do NOT call `adminAllowed()`. Do NOT consult any query parameter. Read the BOOT pin at request time so the gate is "physical access right now", not "physical access at boot".
    - _Requirements: 4.2, 4.3, 4.4_
  - [ ]* 9.2 Verify the handler
    - `grep -n "void handleAdminKeyReveal()" firmware/firmware.ino` → 1 hit
    - `grep -n "digitalRead(BOOT_BUTTON) == LOW" firmware/firmware.ino` → at least 1 hit inside `handleAdminKeyReveal`
    - `grep -n "Hold the BOOT button" firmware/firmware.ino` → 1 hit
    - _Requirements: 4.2, 4.3, 4.4_

- [ ] 10. Wire `setup()` to load the active key, register the new route, and update the boot banner
  - File: `firmware/firmware.ino`, Section 11 ("Entry points"), inside `setup()`
  - [ ] 10.1 Immediately after `loadSettings();`, add `activeAdminKey = loadActiveAdminKey();`
    - _Requirements: 1.3, 2.1_
  - [ ] 10.2 In the route registration block, add `server.on("/admin/key", handleAdminKeyReveal);` alongside the other `server.on(...)` lines (after `handleResetCount` registration is a good location)
    - _Requirements: 4.1_
  - [ ] 10.3 Replace `Serial.println(ADMIN_KEY);` (the boot banner line that prints the admin contacts URL) with `Serial.println(activeAdminKey);` so the banner prints the active key
    - _Requirements: 2.3_
  - [ ]* 10.4 Verify setup wiring
    - `grep -n "activeAdminKey = loadActiveAdminKey()" firmware/firmware.ino` → 1 hit (inside `setup`)
    - `grep -n "server.on(\"/admin/key\", handleAdminKeyReveal)" firmware/firmware.ino` → exactly 1 hit
    - `grep -n "Serial.println(activeAdminKey)" firmware/firmware.ino` → at least 1 hit (boot banner)
    - `grep -n "ADMIN_KEY" firmware/firmware.ino` → 0 hits anywhere in the file
    - _Requirements: 1.3, 2.1, 2.3, 4.1_

- [ ] 11. Wire `loop()` to drain Serial input into the command dispatcher
  - File: `firmware/firmware.ino`, Section 11, inside `loop()`
  - [ ] 11.1 At the very top of `loop()`, before `dnsServer.processNextRequest();`, insert a `while (Serial.available() > 0) { ... }` block that:
    - Reads one byte at a time
    - On `'\n'` or `'\r'`: when `serialLine.length() > 0`, calls `processSerialLine(serialLine)` and resets `serialLine = "";`
    - Otherwise: appends the byte to `serialLine` only when `serialLine.length() < SERIAL_LINE_MAX`; bytes beyond the cap are dropped, and the next newline still resets the buffer
    - _Requirements: 3.1, 3.2_
  - [ ]* 11.2 Verify the loop drain
    - `grep -n "Serial.available()" firmware/firmware.ino` → 1 hit inside `loop`
    - `grep -n "processSerialLine(serialLine)" firmware/firmware.ino` → exactly 1 hit
    - `grep -n "SERIAL_LINE_MAX" firmware/firmware.ino` → at least 2 hits (the constant declaration and the loop guard)
    - _Requirements: 3.1, 3.2_

- [ ] 12. Final structural verification of all changes
  - File: `firmware/firmware.ino`
  - This task is a code review pass that confirms the entire feature landed as specified. Run the grep checks below from the repo root.
  - [ ]* 12.1 Confirm the old admin key is fully removed
    - `grep -n "meshadmin" firmware/firmware.ino` → 0 hits
    - `grep -nE "(const char\\*|const char \\*)\\s*ADMIN_KEY" firmware/firmware.ino` → 0 hits
    - `grep -n "ADMIN_KEY" firmware/firmware.ino` → 0 hits anywhere
    - _Requirements: 1.1, 1.4_
  - [ ]* 12.2 Confirm `activeAdminKey` is referenced from every required call site
    - `grep -n "activeAdminKey" firmware/firmware.ino` should show: declaration in Section 4, assignment inside `setup()` after `loadSettings()`, comparison inside `adminAllowed()`, two embeds inside `adminContactsPage()`, the redirect inside `handleClearContacts`, the body of `handleAdminKeyReveal`, the boot banner `Serial.println`, and the assignments inside `processSerialLine` (the `setkey=` and `clearkey` branches)
    - _Requirements: 1.3, 2.1, 2.2, 2.3, 4.2, 5.1, 5.4_
  - [ ]* 12.3 Confirm the new route is registered exactly once
    - `grep -n "/admin/key" firmware/firmware.ino` → 1 hit (the `server.on` registration)
    - `grep -n "handleAdminKeyReveal" firmware/firmware.ino` → 2 hits (definition and registration)
    - _Requirements: 4.1_
  - [ ]* 12.4 Confirm `addActivity` is called only from the four documented choke points
    - `grep -nc "addActivity(" firmware/firmware.ino` → exactly 5 (1 definition + 4 call sites)
    - The 4 call sites are: `triggerReaction` (`ACTIVITY_REACTION`), the level-up branch inside `triggerReaction` (`ACTIVITY_LEVEL_UP`), `handleContactSubmit` (`ACTIVITY_CONTACT`), and the first-sighting branch of `rememberPeer` (`ACTIVITY_PEER`)
    - **Validates: Property 5 (activity choke-point invariants)**
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5_
  - [ ]* 12.5 Confirm the new types and HTML headings are present
    - `grep -n "struct PeerRecord" firmware/firmware.ino` → 1 hit
    - `grep -n "struct ActivityEntry" firmware/firmware.ino` → 1 hit
    - `grep -n "enum ActivityCategory" firmware/firmware.ino` → 1 hit
    - `grep -n "<h2>Nearby Badges</h2>" firmware/firmware.ino` → 1 hit
    - `grep -n "<h2>Recent Activity</h2>" firmware/firmware.ino` → 1 hit
    - **Validates: Properties 1–5 (structural prerequisites)**
    - _Requirements: 6.1, 7.1, 9.1, 9.2, 11.1_
  - [ ]* 12.6 Confirm preservation of unchanged behavior
    - `grep -n "AP_SSID = \"AI-BADGE\"" firmware/firmware.ino` → 1 hit
    - `grep -n "BLE_BADGE_PREFIX" firmware/firmware.ino` → declaration + the prefix filter inside `rememberPeer`
    - `grep -n "MAX_CONTACTS" firmware/firmware.ino` → declaration + handler guards
    - `grep -n "saveSettings\\|loadSettings" firmware/firmware.ino` → existing call sites unchanged
    - _Requirements: 12.1, 12.2, 12.3, 12.4, 12.5, 12.6, 12.7_

## Notes

- This sketch is single-file Arduino C++ targeting the ESP32-C3 with the Arduino-ESP32 core. There is no host-compilable test harness, so test sub-tasks (marked with `*`) are structural `grep` checks rather than executable property tests, matching the design's Testing Strategy.
- Tasks marked with `*` are optional and can be skipped, but the verification greps catch most regressions cheaply and are recommended before flashing.
- Each task is small and ordered so the file remains close to compilable between steps. The single deliberate exception is task 2 (removing `ADMIN_KEY`), which leaves dangling references until task 3 lands `activeAdminKey`. Apply tasks 1 → 2 → 3 in sequence to minimize compile-error windows.
- All five Correctness Properties from the design are referenced by the optional verification sub-tasks. They serve as documentation of the contracts that an eventual host-compilable port should validate executably.
- After completing the implementation, flashing and on-device verification (fresh-flash boot prints an 8-character lowercase-hex key on Serial; `setkey=foo` and `clearkey` round-trip; `/admin/key` returns 403 without BOOT held and 200 with it held; nearby peers render under "Nearby Badges"; reactions, contact submissions, and peer first-sightings render newest-first under "Recent Activity") is a manual user step and is not part of this code-only task plan.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["1.2"] },
    { "id": 2, "tasks": ["1.3"] },
    { "id": 3, "tasks": ["1.4"] },
    { "id": 4, "tasks": ["2.1"] },
    { "id": 5, "tasks": ["3.1"] },
    { "id": 6, "tasks": ["3.2"] },
    { "id": 7, "tasks": ["3.3"] },
    { "id": 8, "tasks": ["3.4"] },
    { "id": 9, "tasks": ["3.5"] },
    { "id": 10, "tasks": ["3.6"] },
    { "id": 11, "tasks": ["3.7"] },
    { "id": 12, "tasks": ["3.8"] },
    { "id": 13, "tasks": ["4.1"] },
    { "id": 14, "tasks": ["4.2"] },
    { "id": 15, "tasks": ["4.3"] },
    { "id": 16, "tasks": ["4.4"] },
    { "id": 17, "tasks": ["4.5"] },
    { "id": 18, "tasks": ["5.1"] },
    { "id": 19, "tasks": ["5.2"] },
    { "id": 20, "tasks": ["6.1"] },
    { "id": 21, "tasks": ["6.2"] },
    { "id": 22, "tasks": ["6.3"] },
    { "id": 23, "tasks": ["7.1"] },
    { "id": 24, "tasks": ["8.1"] },
    { "id": 25, "tasks": ["8.2"] },
    { "id": 26, "tasks": ["9.1"] },
    { "id": 27, "tasks": ["10.1"] },
    { "id": 28, "tasks": ["10.2"] },
    { "id": 29, "tasks": ["10.3"] },
    { "id": 30, "tasks": ["11.1"] },
    { "id": 31, "tasks": [
      "1.5", "2.2", "3.9", "4.6", "5.3", "6.4",
      "7.2", "8.3", "9.2", "10.4", "11.2",
      "12.1", "12.2", "12.3", "12.4", "12.5", "12.6"
    ] }
  ]
}
```

