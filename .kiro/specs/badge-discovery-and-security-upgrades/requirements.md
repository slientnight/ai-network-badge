# Requirements Document

## Introduction

This feature bundles three cohesive enhancements to the existing ESP32-C3 badge firmware (`firmware/firmware.ino`) so they ship together as one cohesive change:

1. A per-badge admin key derived from the chip MAC, with an optional NVS override managed exclusively over the USB Serial console, replacing the hardcoded `ADMIN_KEY = "meshadmin"` constant.
2. A "Nearby Badges" list on the badge home page that shows each peer's name, an RSSI bucket label with raw dBm, and a relative first-seen time.
3. A "Recent Activity" feed on the home page implemented as an in-memory ring buffer that records reactions, contact submissions, peer sightings, and level-ups.

A shared relative-time helper formats millisecond-deltas across the new UI sections. All existing badge behavior — Wi-Fi soft AP, captive portal, NeoPixel idle and reaction patterns, BLE peer discovery, contact form, mesh game progression, and NVS persistence — is preserved unchanged.

## Glossary

- **Badge_Firmware**: The Arduino sketch in `firmware/firmware.ino` running on the ESP32-C3 badge.
- **Active_Admin_Key**: The admin key currently used by `adminAllowed()` to authorize `/contacts`, `/contacts.csv`, `/clearcontacts`, and `/admin/key`. Either the NVS-stored override (when present and non-empty) or the MAC-derived default.
- **MAC_Derived_Key**: An 8-character lowercase hexadecimal string formed from the last 4 bytes of the ESP32-C3 chip MAC address.
- **NVS_Admin_Key**: The optional admin key stored in the existing NVS namespace `"badge"` under the key name `"adminKey"`.
- **Boot_Button**: The physical BOOT button on the ESP32-C3, wired to GPIO 9 with `INPUT_PULLUP` (pressed = LOW).
- **Serial_Console**: The USB CDC Serial interface at 115200 baud used for `setkey=<value>` and `clearkey` commands.
- **Peer_Record**: An in-RAM struct stored in the `seenPeers[]` array for each unique peer badge name, containing `name`, `rssiFirst`, `rssiLast`, `firstSeenMs`, and `lastSeenMs`.
- **RSSI_Label**: The friendly bucket label derived from a raw dBm value: `"Near"` for `rssi >= -60`, `"Far"` for `-80 <= rssi < -60`, and `"Distant"` for `rssi < -80`.
- **Activity_Entry**: A record in the Recent Activity ring buffer containing a label string, a `millis()` timestamp captured at insertion, and a category enum value.
- **Activity_Category**: One of `REACTION`, `CONTACT`, `PEER`, or `LEVEL_UP`.
- **Activity_Buffer**: The fixed-size in-RAM ring buffer of 8 `Activity_Entry` records with a head index, where the oldest entry is overwritten when full.
- **Relative_Time_String**: The human-readable rendering of a `millis()` delta produced by the shared helper, using the buckets defined in Requirement 8.
- **Home_Page**: The HTML response returned by `htmlPage()` for `GET /`.

## Requirements

### Requirement 1: MAC-Derived Default Admin Key

**User Story:** As the badge owner, I want each badge to boot with a unique admin key derived from its own chip MAC, so that no two badges share the default `meshadmin` key out of the box.

#### Acceptance Criteria

1. THE Badge_Firmware SHALL remove the `ADMIN_KEY` constant from the CONFIG block of `firmware/firmware.ino`.
2. THE Badge_Firmware SHALL compute the MAC_Derived_Key as exactly 8 lowercase hexadecimal characters formed from the last 4 bytes of the ESP32-C3 chip MAC address.
3. WHEN the Badge_Firmware boots and no NVS_Admin_Key is present or the stored NVS_Admin_Key is an empty string, THE Badge_Firmware SHALL set the Active_Admin_Key to the MAC_Derived_Key.
4. THE Badge_Firmware SHALL NOT contain the literal string `"meshadmin"` anywhere in the source code after this feature is applied.

### Requirement 2: NVS Override Of Admin Key

**User Story:** As the badge owner, I want to optionally override the MAC-derived admin key with my own value stored in NVS, so that I can rotate the key without reflashing.

#### Acceptance Criteria

1. WHEN the Badge_Firmware boots and the NVS namespace `"badge"` contains a non-empty string under the key name `"adminKey"`, THE Badge_Firmware SHALL set the Active_Admin_Key to that stored value.
2. WHILE an NVS_Admin_Key override is active, THE Badge_Firmware SHALL use the override value (not the MAC_Derived_Key) for every Active_Admin_Key comparison performed by `adminAllowed()`.
3. WHEN the Badge_Firmware boots, THE Badge_Firmware SHALL print the Active_Admin_Key to the Serial_Console as part of the existing boot banner, replacing the prior banner output that printed the hardcoded `ADMIN_KEY` value.

### Requirement 3: Serial Commands For Admin Key Management

**User Story:** As the badge owner, I want to set or clear the admin key from the USB serial console, so that I can rotate the key in the field without exposing a web form.

#### Acceptance Criteria

1. WHEN a line received on the Serial_Console begins with `setkey=` and the remainder after the `=` is a non-empty string after trimming surrounding whitespace and CR/LF characters, THE Badge_Firmware SHALL store that remainder as the NVS_Admin_Key under namespace `"badge"` key name `"adminKey"` and update the Active_Admin_Key to that value.
2. WHEN a line received on the Serial_Console equals `clearkey` after trimming surrounding whitespace and CR/LF characters, THE Badge_Firmware SHALL remove the NVS_Admin_Key from NVS and update the Active_Admin_Key to the MAC_Derived_Key.
3. WHEN the Badge_Firmware processes a `setkey=` or `clearkey` Serial_Console command, THE Badge_Firmware SHALL print the resulting Active_Admin_Key to the Serial_Console as confirmation.
4. IF a line received on the Serial_Console begins with `setkey=` but the remainder after the `=` is empty after trimming whitespace and CR/LF characters, THEN THE Badge_Firmware SHALL leave the NVS_Admin_Key and Active_Admin_Key unchanged and print an error message to the Serial_Console.
5. THE Badge_Firmware SHALL NOT expose any HTTP route, HTML form, or query parameter for setting or clearing the admin key.

### Requirement 4: Admin Key Reveal Endpoint Gated By BOOT Button

**User Story:** As the badge owner, I want to retrieve the active admin key from the badge web UI only while I am physically holding the BOOT button, so that someone on the AP cannot read the key without physical access to the badge.

#### Acceptance Criteria

1. THE Badge_Firmware SHALL register an HTTP route `GET /admin/key` on the existing `WebServer` instance.
2. WHILE the Boot_Button reads LOW (pressed) at the moment a `GET /admin/key` request is handled, THE Badge_Firmware SHALL respond with HTTP status 200, content type `text/plain`, and a body containing the Active_Admin_Key.
3. IF the Boot_Button reads HIGH (not pressed) at the moment a `GET /admin/key` request is handled, THEN THE Badge_Firmware SHALL respond with HTTP status 403, content type `text/plain`, and a body that instructs the requester to hold the BOOT button on the badge and retry.
4. THE `GET /admin/key` route SHALL NOT require the `key` query argument, and SHALL NOT call `adminAllowed()` to gate the response.

### Requirement 5: Existing Admin Routes Honor The Active Admin Key

**User Story:** As the badge owner, I want the existing admin pages to keep working with whichever admin key is currently active, so that the new key model does not break the contacts admin flow.

#### Acceptance Criteria

1. THE Badge_Firmware SHALL update `adminAllowed()` to compare `server.arg("key")` against the Active_Admin_Key.
2. WHEN a request arrives at `/contacts`, `/contacts.csv`, or `/clearcontacts` with a `key` query argument equal to the Active_Admin_Key, THE Badge_Firmware SHALL serve the existing admin response for that route.
3. IF a request arrives at `/contacts`, `/contacts.csv`, or `/clearcontacts` without a `key` query argument or with a `key` value that does not equal the Active_Admin_Key, THEN THE Badge_Firmware SHALL respond with HTTP status 403 and content type `text/plain`.
4. WHEN the admin contacts page (`adminContactsPage()`) renders the "Download CSV" and "Clear contacts" links, THE Badge_Firmware SHALL embed the Active_Admin_Key as the `key` query argument in those link URLs.

### Requirement 6: Nearby Badges Peer Records

**User Story:** As the badge owner, I want each remembered peer to retain its first and most recent RSSI plus first and last sighting times, so that the home page can show how strong and how recent each contact was.

#### Acceptance Criteria

1. THE Badge_Firmware SHALL replace the existing `seenPeers` storage of type `String[MAX_SEEN_PEERS]` with an array of Peer_Record structs sized at `MAX_SEEN_PEERS`, where each Peer_Record contains `name`, `rssiFirst`, `rssiLast`, `firstSeenMs`, and `lastSeenMs`.
2. WHEN `rememberPeer()` is invoked with a peer name that passes the existing prefix and self-name filters and is not already present in `seenPeers`, THE Badge_Firmware SHALL append a new Peer_Record whose `name` equals the peer name, whose `rssiFirst` and `rssiLast` equal the RSSI returned by `BLEAdvertisedDevice::getRSSI()` for that advertisement, and whose `firstSeenMs` and `lastSeenMs` equal the current `millis()` value.
3. WHEN `rememberPeer()` is invoked with a peer name that is already present in `seenPeers`, THE Badge_Firmware SHALL update the existing Peer_Record so that `rssiLast` equals the RSSI from the current advertisement and `lastSeenMs` equals the current `millis()` value, without modifying `rssiFirst` or `firstSeenMs`.
4. WHILE the badge is powered on, THE Badge_Firmware SHALL keep the Peer_Record array in RAM only and SHALL NOT persist Peer_Record fields to NVS.
5. WHEN a new peer would be appended and `seenPeers` is already full at `MAX_SEEN_PEERS` records, THE Badge_Firmware SHALL preserve the existing overwrite-slot-zero behavior currently used for the `String[]` storage, replacing slot 0 with a freshly populated Peer_Record for the new peer.

### Requirement 7: Nearby Badges Section On Home Page

**User Story:** As a badge visitor, I want the home page to show the badges seen nearby with a friendly RSSI label and how long ago each was first seen, so that I can tell who is around and how close they are.

#### Acceptance Criteria

1. THE Badge_Firmware SHALL render a "Nearby Badges" section in `htmlPage()` that lists every populated Peer_Record currently held in `seenPeers`.
2. THE Badge_Firmware SHALL render each peer entry with the peer name, an RSSI_Label followed by the raw `rssiLast` value in dBm in parentheses formatted as `"<Label> (<rssi> dBm)"`, and a Relative_Time_String describing how long ago `firstSeenMs` was relative to the current `millis()` value.
3. WHILE `rssiLast` for a peer is greater than or equal to `-60`, THE Badge_Firmware SHALL use `"Near"` as that peer's RSSI_Label.
4. WHILE `rssiLast` for a peer is greater than or equal to `-80` and less than `-60`, THE Badge_Firmware SHALL use `"Far"` as that peer's RSSI_Label.
5. WHILE `rssiLast` for a peer is less than `-80`, THE Badge_Firmware SHALL use `"Distant"` as that peer's RSSI_Label.
6. WHEN `htmlPage()` is rendered and `seenPeers` contains zero populated Peer_Records, THE Badge_Firmware SHALL render the "Nearby Badges" section with placeholder text indicating that no peers have been seen yet.
7. THE Badge_Firmware SHALL escape every peer name with `escapeHtml()` before including the name in the HTML response.

### Requirement 8: Relative Time Helper

**User Story:** As a badge visitor, I want timestamps in the Nearby Badges and Recent Activity sections to read like "5 min ago", so that I can quickly tell how recent each event is.

#### Acceptance Criteria

1. THE Badge_Firmware SHALL provide a Relative_Time_String helper that takes a past `millis()` timestamp and returns a `String` describing the elapsed duration relative to the current `millis()` value.
2. WHEN the elapsed duration is less than 10 seconds, THE Badge_Firmware SHALL return the literal string `"just now"`.
3. WHEN the elapsed duration is greater than or equal to 10 seconds and less than 60 seconds, THE Badge_Firmware SHALL return the elapsed whole seconds followed by `" sec ago"`.
4. WHEN the elapsed duration is greater than or equal to 60 seconds and less than 60 minutes, THE Badge_Firmware SHALL return the elapsed whole minutes followed by `" min ago"`.
5. WHEN the elapsed duration is greater than or equal to 60 minutes and less than 24 hours, THE Badge_Firmware SHALL return the elapsed whole hours followed by `" hr ago"`.
6. WHEN the elapsed duration is greater than or equal to 24 hours, THE Badge_Firmware SHALL return the literal string `"24+ hr ago"`.
7. IF the supplied past `millis()` timestamp is greater than the current `millis()` value (because of unsigned wrap or unset timestamps), THEN THE Badge_Firmware SHALL return the literal string `"just now"`.

### Requirement 9: Recent Activity Ring Buffer

**User Story:** As the badge owner, I want the badge to remember the last several notable events in a ring buffer, so that the home page can show a short activity history without persisting it.

#### Acceptance Criteria

1. THE Badge_Firmware SHALL define an Activity_Entry struct containing a `String` label, an `unsigned long` timestamp captured from `millis()` at insertion, and an Activity_Category enum field.
2. THE Badge_Firmware SHALL define the Activity_Category enum with exactly the values `REACTION`, `CONTACT`, `PEER`, and `LEVEL_UP`.
3. THE Badge_Firmware SHALL maintain an Activity_Buffer sized at exactly 8 Activity_Entry slots together with a head index that identifies the next slot to write.
4. WHEN a new Activity_Entry is added and fewer than 8 entries are currently stored, THE Badge_Firmware SHALL store the entry at the head slot, advance the head, and increase the stored count by one.
5. WHEN a new Activity_Entry is added and 8 entries are currently stored, THE Badge_Firmware SHALL overwrite the oldest entry by writing into the head slot and advancing the head, while keeping the stored count at 8.
6. WHILE the badge is powered on, THE Badge_Firmware SHALL keep Activity_Buffer in RAM only and SHALL NOT persist Activity_Buffer entries to NVS.

### Requirement 10: Activity Choke Points

**User Story:** As the badge owner, I want activity entries to be added at the same code locations that already drive UX feedback, so that the activity feed reflects every meaningful event without duplicating logic.

#### Acceptance Criteria

1. WHEN `triggerReaction()` is invoked with a `Reaction` value other than `REACTION_NONE` and the invocation does not cross a level-up threshold, THE Badge_Firmware SHALL append one Activity_Entry with category `REACTION` and a label containing the reaction effect name corresponding to the supplied `Reaction`.
2. WHEN `triggerReaction()` is invoked and the resulting `packetCount` crosses a level-up threshold as detected by `crossedUnlock()`, THE Badge_Firmware SHALL append one Activity_Entry with category `LEVEL_UP` and a label that names the new badge level returned by `badgeLevelName()`.
3. WHEN `handleContactSubmit()` accepts a non-empty contact submission and increments `contactPacketCount`, THE Badge_Firmware SHALL append one Activity_Entry with category `CONTACT` and a label that identifies the submitted contact (using the submitted name when non-empty, otherwise a generic contact label).
4. WHEN `rememberPeer()` records a peer that was not previously present in `seenPeers`, THE Badge_Firmware SHALL append one Activity_Entry with category `PEER` and a label that includes the peer name.
5. WHEN `rememberPeer()` is invoked for a peer that is already present in `seenPeers`, THE Badge_Firmware SHALL NOT append a new Activity_Entry for that sighting.

### Requirement 11: Recent Activity Section On Home Page

**User Story:** As a badge visitor, I want the home page to show the most recent activity newest-first with relative timestamps, so that I can see what just happened on the badge.

#### Acceptance Criteria

1. THE Badge_Firmware SHALL render a "Recent Activity" section in `htmlPage()` that lists the Activity_Entry records currently held in Activity_Buffer.
2. THE Badge_Firmware SHALL render Activity_Entry records in newest-first order, where the most recently inserted entry appears at the top of the list.
3. THE Badge_Firmware SHALL render each Activity_Entry as its label followed by the Relative_Time_String for its stored timestamp.
4. THE Badge_Firmware SHALL escape every Activity_Entry label with `escapeHtml()` before including the label in the HTML response.
5. WHEN `htmlPage()` is rendered and Activity_Buffer contains zero entries, THE Badge_Firmware SHALL render the "Recent Activity" section with placeholder text indicating that no activity has been recorded yet.
6. THE Badge_Firmware SHALL retain the existing `lastSignal` global variable and SHALL continue to render `lastSignal` in its current location on the Home_Page as a quick-glance summary alongside the new "Recent Activity" section.

### Requirement 12: Preservation Of Existing Behavior

**User Story:** As the badge owner, I want all current badge behavior to remain unchanged after these upgrades land, so that adding admin-key, peer, and activity features does not regress the existing experience.

#### Acceptance Criteria

1. THE Badge_Firmware SHALL preserve the existing Wi-Fi soft AP configuration, including SSID `AI-BADGE`, open network mode, and IP `192.168.4.1`.
2. THE Badge_Firmware SHALL preserve the existing captive portal probe handling for Android, Apple, and Windows detection URLs.
3. THE Badge_Firmware SHALL preserve the existing NeoPixel idle patterns and reaction effects, including their selection logic, brightness range, and timing constants.
4. THE Badge_Firmware SHALL preserve the existing BLE advertising name, scan interval, peer prefix filter, and self-name filter.
5. THE Badge_Firmware SHALL preserve the existing `/contact` POST flow, the contact storage cap of `MAX_CONTACTS`, and the persisted NVS keys used for stored contacts.
6. THE Badge_Firmware SHALL preserve the existing mesh game progression logic in `triggerReaction()`, `crossedUnlock()`, `badgeLevelName()`, `badgeLevelMeaning()`, and `nextUnlockText()`.
7. THE Badge_Firmware SHALL preserve persistence of `brightness`, `idlePattern`, `packetCount`, `contactCount`, and `peerSeen` via the existing `saveSettings()` and `loadSettings()` functions.
