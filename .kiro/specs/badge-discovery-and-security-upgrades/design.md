# Design Document

## Overview

This is a layered upgrade on top of the existing single-file sketch. Three concerns ship together:

1. A per-badge admin key derived from the chip MAC, with an optional NVS override managed only over the USB Serial console. The hardcoded `ADMIN_KEY = "meshadmin"` constant is removed.
2. Replacing the volatile `String seenPeers[]` dedupe ring with `PeerRecord seenPeers[]`, and rendering a "Nearby Badges" section on the home page with friendly RSSI labels and relative first-seen time.
3. An in-RAM 8-slot "Recent Activity" ring buffer fed from four choke points (reactions, level-ups, contact submissions, first peer sightings) and rendered on the home page.

No new files. All work lands inside `firmware/firmware.ino`. Existing behavior — SoftAP, captive portal, NeoPixel idle/reaction patterns, BLE scan timings, contact form, mesh game progression, NVS persistence — is preserved verbatim.

## Architecture

### File Layout

The single-file sketch structure is unchanged. New code lands inside the existing numbered sections:

```
firmware/firmware.ino
├── Section 1: Includes               (no change)
├── CONFIG block                      (REMOVE: ADMIN_KEY)
├── Section 3: Hardware and limits    (no change)
├── Section 4: Globals                (ADD: PeerRecord struct + array,
│                                            ActivityCategory enum,
│                                            ActivityEntry struct + ring,
│                                            activeAdminKey,
│                                            serial input line buffer)
├── Section 5: Helpers                (ADD: macDerivedAdminKey,
│                                            loadActiveAdminKey,
│                                            relativeTimeString,
│                                            rssiLabel,
│                                            addActivity,
│                                            reactionName)
│                                     (CHANGE: rememberPeer signature,
│                                              BadgeAdvertisedDeviceCallbacks::onResult,
│                                              adminAllowed)
├── Section 6: Idle patterns          (no change)
├── Section 7: Reaction effects       (CHANGE: triggerReaction emits activity entries)
├── Section 8: HTML page builders     (CHANGE: htmlPage adds Nearby Badges and
│                                              Recent Activity sections;
│                                              adminContactsPage uses activeAdminKey)
├── Section 9: HTTP handlers          (CHANGE: handleContactSubmit emits activity entry,
│                                              handleClearContacts redirects with activeAdminKey)
│                                     (ADD: handleAdminKeyReveal)
├── Section 10: Input                 (no change)
└── Section 11: Entry points          (CHANGE: setup() calls loadActiveAdminKey,
│                                              registers /admin/key,
│                                              banner prints activeAdminKey)
                                      (CHANGE: loop() drains Serial, dispatches
                                               setkey= and clearkey commands)
```

The internal definition order inside each section continues to be "every helper before its first caller", matching the existing sketch.

## Components and Interfaces

### New globals (Section 4)

| Variable                              | Type                          | Initial      | Notes                                                                 |
| ------------------------------------- | ----------------------------- | ------------ | --------------------------------------------------------------------- |
| `activeAdminKey`                      | `String`                      | `""`         | Resolved in `setup()` by `loadActiveAdminKey()`. Read by `adminAllowed()`, `adminContactsPage()`, `handleClearContacts`, `handleAdminKeyReveal`, and the boot banner. |
| `seenPeers[MAX_SEEN_PEERS]`           | `PeerRecord[12]`              | zero-initialized | Replaces the existing `String seenPeers[]`. `seenPeerSlots` is reused as the populated count. |
| `activityBuffer[ACTIVITY_BUFFER_SIZE]`| `ActivityEntry[8]`            | zero-initialized | Recent Activity ring buffer.                                         |
| `activityHead`                        | `uint8_t`                     | `0`          | Next slot to write. Always in `[0, ACTIVITY_BUFFER_SIZE)`.            |
| `activityCount`                       | `uint8_t`                     | `0`          | Populated entries. Always in `[0, ACTIVITY_BUFFER_SIZE]`.             |
| `serialLine`                          | `String`                      | `""`         | Accumulator for incoming Serial bytes between newlines.               |

New `const` next to the other limits in Section 3:

```cpp
const uint8_t ACTIVITY_BUFFER_SIZE = 8;
const uint16_t SERIAL_LINE_MAX = 96;   // hard cap on serialLine length
```

### New / changed types (Section 4)

```cpp
struct PeerRecord {
  String name;            // empty => slot unused
  int8_t rssiFirst;       // dBm at first sighting
  int8_t rssiLast;        // dBm at most recent sighting
  unsigned long firstSeenMs;
  unsigned long lastSeenMs;
};

enum ActivityCategory {
  ACTIVITY_REACTION,
  ACTIVITY_CONTACT,
  ACTIVITY_PEER,
  ACTIVITY_LEVEL_UP
};

struct ActivityEntry {
  String label;
  unsigned long timestamp;   // millis() captured at insertion
  ActivityCategory category;
};
```

The existing `Reaction` enum and `seenPeerSlots` counter are unchanged.

### New helpers (Section 5)

Defined in this order, each before its first caller:

- `String macDerivedAdminKey()` — pure helper. Reads the chip MAC via `WiFi.macAddress()` (or `esp_efuse_mac_get_default` if WiFi is not yet up), takes the last 4 bytes, and formats them as exactly 8 lowercase hex characters using `snprintf("%02x%02x%02x%02x", ...)`. Returns the `String`. No side effects, no NVS access.
- `String loadActiveAdminKey()` — reads `prefs.getString("adminKey", "")`. If the result is non-empty, returns it. Otherwise returns `macDerivedAdminKey()`. Called from `setup()` exactly once after `prefs.begin("badge", false)` and from the Serial command dispatcher whenever the stored key changes.
- `String relativeTimeString(unsigned long pastMs)` — computes `now - pastMs` using `unsigned long` subtraction. If `pastMs > now` (caller passed an unset or wrapped value), returns `"just now"`. Otherwise applies the bucket table from Requirement 8: `< 10s → "just now"`, `[10s, 60s) → "<n> sec ago"`, `[60s, 60min) → "<n> min ago"`, `[60min, 24h) → "<n> hr ago"`, `>= 24h → "24+ hr ago"`. Whole-unit rounding via integer division.
- `String rssiLabel(int8_t rssi)` — returns `"Near"` when `rssi >= -60`, `"Far"` when `-80 <= rssi < -60`, `"Distant"` when `rssi < -80`. Pure helper.
- `void addActivity(ActivityCategory category, String label)` — single insertion choke point. Writes `{label, millis(), category}` into `activityBuffer[activityHead]`, advances `activityHead = (activityHead + 1) % ACTIVITY_BUFFER_SIZE`, and bumps `activityCount` up to a maximum of `ACTIVITY_BUFFER_SIZE`. No NVS writes.
- `String reactionName(Reaction r)` — small mapper from `Reaction` enum values to the human label used in activity entries (e.g. `REACTION_PACKET → "Packet"`, `REACTION_LINKUP → "Link"`, `REACTION_AI → "AI inference"`, etc.). Used only by `triggerReaction`.

### Changed helpers (Section 5)

- **`rememberPeer(String peerName, int8_t rssi)`** — signature gains a second parameter. Existing prefix/self-name filters and full-buffer overwrite-slot-zero behavior are preserved exactly. New behavior:
  - First sighting (peer name not present): populate a fresh `PeerRecord` with `name = peerName`, `rssiFirst = rssiLast = rssi`, `firstSeenMs = lastSeenMs = millis()`. Append to `seenPeers[seenPeerSlots]`, or overwrite `seenPeers[0]` when `seenPeerSlots == MAX_SEEN_PEERS` (matching the existing ring behavior). Increment `peerSeenCount`, set `lastSignal`, `saveSettings()`, fire `REACTION_LINKUP`. **Then call `addActivity(ACTIVITY_PEER, "Peer: " + peerName)`** (only on this branch).
  - Repeat sighting (peer name already present): update the existing record's `rssiLast = rssi` and `lastSeenMs = millis()`. Do not modify `rssiFirst`, `firstSeenMs`, `peerSeenCount`, `lastSignal`, the reaction state, NVS, or the activity buffer.
- **`BadgeAdvertisedDeviceCallbacks::onResult`** — passes `advertisedDevice.getRSSI()` (cast to `int8_t`) as the new second argument when calling `rememberPeer`.
- **`adminAllowed()`** — body becomes `return server.hasArg("key") && server.arg("key") == activeAdminKey;`. The removed `ADMIN_KEY` constant is no longer referenced anywhere.

### Changed reaction code (Section 7)

- **`triggerReaction(Reaction r, String signalName, uint8_t packetValue)`** — the existing body keeps its order of operations (set `activeReaction`, `reactionStart`, `lastSignal`, then increment `packetCount` and detect level-up). Two new calls:
  1. Right after `activeReaction = r` and `lastSignal` is set, when `r != REACTION_NONE`, call `addActivity(ACTIVITY_REACTION, reactionName(r))`. This fires for every meaningful trigger, including the boot reaction and the contact reaction.
  2. Inside the `crossedUnlock(before, packetCount)` branch, after the storm promotion and before `saveSettings()`, call `addActivity(ACTIVITY_LEVEL_UP, "Level up: " + badgeLevelName())`. Both REACTION and LEVEL_UP entries can fire for the same call when applicable, in that order.

### Changed HTML builders (Section 8)

`htmlPage()` gains two new sections, both inside the same `.card` container, placed immediately after the existing "Connect" buttons block and before the "Send a packet" grid so the new information is high on the page.

Rough HTML structure (no full code; reuses existing `.game`/`.formbox`/`.pill`/`.small` styles):

```
<h2>Nearby Badges</h2>
<div class='game'>
  -- if seenPeerSlots == 0: <p class='small'>No peers seen yet.</p>
  -- else: for each populated PeerRecord:
       <p>
         <b>{escapeHtml(name)}</b>
         <span class='pill'>{rssiLabel(rssiLast)} ({rssiLast} dBm)</span>
         <span class='small'>{relativeTimeString(firstSeenMs)}</span>
       </p>
</div>

<h2>Recent Activity</h2>
<div class='game'>
  -- if activityCount == 0: <p class='small'>No activity yet.</p>
  -- else: for each entry in newest-first order:
       <p>
         {escapeHtml(label)}
         <span class='small'>{relativeTimeString(timestamp)}</span>
       </p>
</div>
```

Newest-first iteration walks indices `(activityHead - 1 + ACTIVITY_BUFFER_SIZE) % ACTIVITY_BUFFER_SIZE` for `activityCount` steps. The existing `lastSignal` pill in the BUILD THE MESH block is preserved unchanged as a quick-glance summary.

`adminContactsPage()` — replaces both `String(ADMIN_KEY)` concatenations in the "Download CSV" and "Clear contacts" links with `activeAdminKey`. No other change.

### Changed HTTP handlers (Section 9)

- **`handleContactSubmit`** — after `contactPacketCount++` and the `triggerReaction(REACTION_CONTACT, ...)` call, append `addActivity(ACTIVITY_CONTACT, name.length() > 0 ? "Contact from " + name : "Contact card received")`. Order matters: triggerReaction runs first so the REACTION entry is older than the CONTACT entry, and both surface in newest-first render order.
- **`handleClearContacts`** — change the redirect line from `String("/contacts?key=") + ADMIN_KEY` to `String("/contacts?key=") + activeAdminKey`.

### New HTTP handler (Section 9)

- **`handleAdminKeyReveal`**:
  ```
  if (digitalRead(BOOT_BUTTON) == LOW) {
    server.send(200, "text/plain", activeAdminKey);
  } else {
    server.send(403, "text/plain", "Hold the BOOT button on the badge and reload this page.");
  }
  ```
  Does not call `adminAllowed()`. Does not consult any query parameter. Reads the BOOT pin at request time so the gate is "physical access right now", not "physical access at boot".

### Route table

One row added to the existing route table:

| Method | Path          | Handler                  |
| ------ | ------------- | ------------------------ |
| GET    | `/admin/key`  | `handleAdminKeyReveal`   |

All other routes (existing app routes plus captive portal probes plus `onNotFound`) are unchanged.

### Entry points (Section 11)

**`setup()` changes** (in order, all inside the existing function):

1. After `prefs.begin("badge", false)` and `loadSettings()`, call `activeAdminKey = loadActiveAdminKey();`.
2. Register the new route alongside the others: `server.on("/admin/key", handleAdminKeyReveal);`.
3. The boot banner block keeps its existing wording but the final `Serial.println(ADMIN_KEY)` becomes `Serial.println(activeAdminKey)`. The line printing the admin contacts URL keeps the `?key=` segment but now uses `activeAdminKey`.

**`loop()` changes** — at the top of the existing `loop()`, before `dnsServer.processNextRequest()`:

```
while (Serial.available() > 0) {
  char ch = Serial.read();
  if (ch == '\n' || ch == '\r') {
    if (serialLine.length() > 0) {
      processSerialLine(serialLine);
      serialLine = "";
    }
  } else if (serialLine.length() < SERIAL_LINE_MAX) {
    serialLine += ch;
  }
  // else: drop the byte; line will be discarded on next newline
  // (overflow-safety; see Edge Cases)
}
```

`processSerialLine(String line)` is a small private helper next to `loadActiveAdminKey` in Section 5:

- Trim whitespace and CR/LF from both ends of `line`.
- If `line.startsWith("setkey=")`:
  - `String value = line.substring(7);` then trim.
  - If `value.length() > 0`: `prefs.putString("adminKey", value); activeAdminKey = value; Serial.println("Admin key set: " + activeAdminKey);`
  - Else: `Serial.println("setkey= requires a non-empty value");`
- Else if `line == "clearkey"`:
  - `prefs.remove("adminKey"); activeAdminKey = macDerivedAdminKey(); Serial.println("Admin key cleared. Active key: " + activeAdminKey);`
- Otherwise: ignore the line silently.

If the accumulator hits `SERIAL_LINE_MAX` before a newline, additional bytes are dropped (not stored, not dispatched). The next newline still resets the buffer, so a single overlong paste cannot wedge subsequent commands.

## Data Models

### `PeerRecord`

| Field         | Type            | Notes                                                                  |
| ------------- | --------------- | ---------------------------------------------------------------------- |
| `name`        | `String`        | Empty string indicates an unused slot.                                 |
| `rssiFirst`   | `int8_t`        | RSSI in dBm captured on first sighting. Never updated after.           |
| `rssiLast`    | `int8_t`        | RSSI in dBm of most recent sighting. Used by `rssiLabel` and the home page. |
| `firstSeenMs` | `unsigned long` | `millis()` at first sighting. Used for relative time on home page.     |
| `lastSeenMs`  | `unsigned long` | `millis()` at most recent sighting. Held for future use.               |

`seenPeerSlots` continues to count populated slots. Slot 0 overwrite-on-full continues to mirror the existing `String[]` ring semantics.

### `ActivityEntry` and ring buffer

```
activityBuffer[8]   ActivityEntry slots
activityHead        next slot to write (uint8_t, mod 8)
activityCount       populated count, capped at 8 (uint8_t)
```

Insertion is single-threaded (called only from web/Serial handlers and `triggerReaction`, all on the Arduino main loop), so no synchronization is needed.

Newest-first iteration:
```
for (uint8_t i = 0; i < activityCount; i++) {
  uint8_t idx = (activityHead + ACTIVITY_BUFFER_SIZE - 1 - i) % ACTIVITY_BUFFER_SIZE;
  // render activityBuffer[idx]
}
```

### NVS schema additions (namespace `badge`)

| Key        | Type     | Source variable     | Notes                                                |
| ---------- | -------- | ------------------- | ---------------------------------------------------- |
| `adminKey` | `String` | `activeAdminKey`    | Optional override. Absent or empty => MAC-derived fallback. Written by `setkey=` Serial command, removed by `clearkey`. |

The existing keys (`brightness`, `idlePattern`, `packetCount`, `contactCount`, `peerSeen`, and `nm##`/`ct##`/`nt##`) are unchanged. `PeerRecord` fields and `ActivityEntry` records are RAM-only and never written to NVS.

## Error Handling

- **Empty NVS `adminKey`** — `loadActiveAdminKey()` treats `""` exactly like a missing key and falls back to `macDerivedAdminKey()`. This mirrors how `prefs.getString("adminKey", "")` itself reports an absent key.
- **Empty `setkey=` value** — `processSerialLine` rejects the command and prints `"setkey= requires a non-empty value"`. Neither `prefs` nor `activeAdminKey` is touched.
- **Malformed Serial line** — anything that does not start with `setkey=` and is not exactly `clearkey` is silently ignored. Trimming covers `\r\n` line endings from typical serial monitors.
- **Serial line buffer overflow** — `serialLine` is capped at `SERIAL_LINE_MAX` (96) bytes; bytes past the cap are dropped. The buffer is cleared on the next newline so one oversized paste does not block subsequent commands. The cap is well above any realistic admin key length but small enough that a stuck producer cannot grow `String` heap allocations unbounded.
- **`relativeTimeString` unsigned wrap** — when `pastMs > millis()` (uninitialized timestamp, post-wrap state, or future timestamp), the helper returns `"just now"` rather than computing a huge bogus delta. This keeps the home page sensible across the ~49-day `millis()` rollover.
- **BOOT button held during `/admin/key`** — the gate is read at request time, not cached. Holding the button across a sequence of requests is the supported way to inspect the key from a phone on the AP.
- **Full peer buffer** — when `seenPeerSlots == MAX_SEEN_PEERS`, a brand-new peer overwrites slot 0's `PeerRecord` in full (all five fields populated as a first sighting). This preserves the existing dedupe-ring behavior; it does not migrate first/last RSSI from the displaced peer.
- **`addActivity` is a single choke point** — every new entry flows through one function, which is the only place that touches `activityHead` and `activityCount`. Callers cannot underflow or skew the count.

## Testing Strategy

Same constraint as the prior spec: there is no host-compilable harness for the sketch, so this iteration adds no automated tests. Verification relies on:

- **Static structural checks** — grep-based checks confirm: the literal `"meshadmin"` is gone, `ADMIN_KEY` is no longer declared, `activeAdminKey` is referenced from `adminAllowed`, `adminContactsPage`, `handleClearContacts`, `handleAdminKeyReveal`, and the boot banner; the new struct/enum/buffer declarations are present; `server.on("/admin/key", ...)` is registered exactly once; `addActivity` is invoked from the four documented choke points and only those; `rememberPeer` calls `addActivity` from the first-sighting branch only.
- **Manual on-device verification** — fresh-flash boot prints an 8-character lowercase-hex key on Serial; `setkey=foo` and `clearkey` round-trip; `/admin/key` returns 403 without BOOT held and 200 with it held; nearby peers appear in the Nearby Badges section with reasonable RSSI labels; reactions, contact submissions, and peer first-sightings all show up newest-first under Recent Activity; the buffer never grows past 8 entries; relative-time strings update naturally over a few minutes of usage.

The properties listed below are the testable contracts that an eventual host-compilable port (or a refactor that extracts these helpers) should validate. They are documentation in this iteration, not executable tests.

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system — essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: RSSI label bucket correctness

*For any* `int8_t` value `rssi`, `rssiLabel(rssi)` returns exactly `"Near"` when `rssi >= -60`, exactly `"Far"` when `-80 <= rssi < -60`, and exactly `"Distant"` when `rssi < -80`, with no other return value possible and no overlap between buckets.

**Validates: Requirements 7.3, 7.4, 7.5**

### Property 2: Relative-time bucket correctness with unsigned-wrap safety

*For any* `unsigned long` value `pastMs` and the current `millis()` reading `now`, `relativeTimeString(pastMs)` returns: `"just now"` when `pastMs > now` (treating unsigned wrap as "no elapsed time") or when `now - pastMs < 10000` ms; `"<n> sec ago"` with `n = (now - pastMs) / 1000` when `10000 <= now - pastMs < 60000`; `"<n> min ago"` with `n = (now - pastMs) / 60000` when `60000 <= now - pastMs < 3600000`; `"<n> hr ago"` with `n = (now - pastMs) / 3600000` when `3600000 <= now - pastMs < 86400000`; and `"24+ hr ago"` when `now - pastMs >= 86400000`.

**Validates: Requirements 8.2, 8.3, 8.4, 8.5, 8.6, 8.7**

### Property 3: Activity buffer cap and head invariants

*For any* finite sequence of `addActivity(category, label)` calls starting from the initial state, after every call: `0 <= activityCount <= ACTIVITY_BUFFER_SIZE` (i.e. never exceeds 8), `activityHead` lies in `[0, ACTIVITY_BUFFER_SIZE)`, and the most recently inserted entry occupies slot `(activityHead + ACTIVITY_BUFFER_SIZE - 1) % ACTIVITY_BUFFER_SIZE`.

**Validates: Requirements 9.3, 9.4, 9.5**

### Property 4: MAC-derived key format

*For any* 6-byte MAC address available to the firmware, `macDerivedAdminKey()` returns a `String` of exactly length 8 whose every character lies in `[0-9a-f]` (lowercase hex), and whose value equals the lowercase hex encoding of the last 4 bytes of that MAC.

**Validates: Requirements 1.2**

### Property 5: Activity choke-point invariants

*For any* execution of the firmware, an `ActivityEntry` is appended to the buffer if and only if one of the following call sites runs to completion: (a) `triggerReaction(r, ...)` with `r != REACTION_NONE` (appends a `REACTION` entry; appends an additional `LEVEL_UP` entry when `crossedUnlock(before, packetCount)` is true); (b) `handleContactSubmit` past its non-blank, non-full guard after incrementing `contactPacketCount`; (c) `rememberPeer` taking the first-sighting branch (a peer name not previously present that passes the prefix and self-name filters). The repeat-sighting branch of `rememberPeer` MUST NOT append an entry. No other code path appends entries.

**Validates: Requirements 10.1, 10.2, 10.3, 10.4, 10.5**

## Out of Scope

- Over-the-air admin key rotation. The key is only mutable from the USB Serial console; no HTTP route, HTML form, or query parameter sets or clears it.
- Persisting `PeerRecord` fields to NVS. Peer first/last RSSI and timestamps are intentionally RAM-only and reset every boot.
- Per-peer RSSI history beyond first and last samples. The design explicitly stores two samples per peer; no rolling average, no histogram, no time series.
- Admin-key complexity rules. `setkey=anything-non-empty` is accepted; users are trusted to pick a reasonable key.
- Persisting the Recent Activity ring buffer. Activity is intentionally RAM-only and resets every boot.
- Refactoring helpers into separate translation units or adding a host-compilable test harness. The sketch remains a single `.ino` file.
