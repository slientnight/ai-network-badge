# Requirements Document

## Introduction

This feature restructures the existing single-file ESP32-C3 badge firmware (`ai_network_badge_firmware_with_contacts.cpp`) into a clean Arduino sketch project named `ai_network_badge/` at the workspace root. The restructured project preserves every runtime feature of the original firmware verbatim (Wi-Fi captive portal, NeoPixel idle and reaction effects, "Build the Mesh" game progression, contact card form with NVS-backed storage, BLE peer discovery, BOOT button cycling, and persistent settings) while reorganizing the source into a single `.ino` sketch with a clearly delimited `CONFIG` block at the top. The feature also adds three Markdown documentation files (`README.md`, `wiring.md`, `troubleshooting.md`) covering both Seeed XIAO ESP32-C3 and ESP32-C3 SuperMini boards as first-class targets.

## Glossary

- **Sketch_Project**: The reorganized Arduino project consisting of `ai_network_badge/ai_network_badge.ino` and the three documentation files at the workspace root.
- **Sketch_File**: The single Arduino source file `ai_network_badge/ai_network_badge.ino` that contains all firmware code.
- **CONFIG_Block**: A clearly delimited, labeled section near the top of `Sketch_File` that groups every user-tunable constant (badge identity, URLs, BLE names, admin key, contact storage limits) with comments describing each constant and which ones to change before an event.
- **Original_Sketch**: The pre-existing source file `ai_network_badge_firmware_with_contacts.cpp` at the workspace root.
- **Captive_Portal**: The combination of Wi-Fi SoftAP `AI-BADGE`, DNSServer on port 53, and HTTP probe handlers that present the badge landing page when a device joins the AP.
- **Idle_Pattern**: One of four NeoPixel animations (`Packet Chase`, `AI Pulse`, `Network Sparkle`, `Rainbow Mesh`) rendered while no reaction is active.
- **Reaction_Effect**: One of seven time-bounded NeoPixel animations (`PACKET`, `LINKUP`, `AI`, `STORM`, `GITHUB`, `LINKEDIN`, `CONTACT`) driven by a 55 ms frame loop and lasting `REACTION_MS` = 4500 ms.
- **Mesh_Game**: The "Build the Mesh" progression that increments `packetCount`, derives a level name (`Offline Node`, `Listening Node`, `Linked Node`, `Mesh Builder`, `AI Router`, `Supernode`), unlocks at thresholds 1/5/10/25/50, and triggers `REACTION_STORM` on level-up.
- **Contact_Form**: The HTTP POST `/contact` form with fields `name` (max 32), `contact` (max 80), `note` (max 120), sanitized via `cleanInput`, capped at `MAX_CONTACTS` = 25, persisted in NVS namespace `badge` with keys `nm##`/`ct##`/`nt##`, and worth `CONTACT_PACKET_VALUE` = 3 packets per submission.
- **Admin_Endpoint**: An HTTP route (`/contacts`, `/contacts.csv`, `/clearcontacts`) that requires the query parameter `?key=<ADMIN_KEY>` to access.
- **BLE_Presence**: The BLE advertising and scanning behavior that advertises as `AI-BADGE-MARSHALL`, scans every 20 s for 3 s, accepts peers whose name starts with `AI-BADGE`, dedupes up to `MAX_SEEN_PEERS` = 12 peers, increments `peerSeenCount`, and triggers `REACTION_LINKUP` on a new peer.
- **NVS_Settings**: Settings persisted via the `Preferences` library in namespace `badge`, including keys `brightness`, `idlePattern`, `packetCount`, `contactCount`, and `peerSeen`.
- **Target_Board**: One of the two supported boards: Seeed XIAO ESP32-C3 or ESP32-C3 SuperMini.
- **README**: The documentation file `README.md` at the workspace root.
- **Wiring_Doc**: The documentation file `wiring.md` at the workspace root.
- **Troubleshooting_Doc**: The documentation file `troubleshooting.md` at the workspace root.

## Requirements

### Requirement 1: Sketch Folder Layout

**User Story:** As the badge developer, I want the firmware reorganized into a single Arduino sketch folder with the matching `.ino` file, so that the project opens directly in the Arduino IDE without manual file renaming.

#### Acceptance Criteria

1. THE Sketch_Project SHALL place the sketch at `ai_network_badge/ai_network_badge.ino` relative to the workspace root.
2. THE Sketch_Project SHALL contain exactly one `.ino` source file inside the `ai_network_badge/` folder.
3. THE Sketch_Project SHALL NOT include separate header (`.h`) or source (`.cpp`) files alongside `ai_network_badge.ino` inside the `ai_network_badge/` folder.
4. WHEN the restructure is complete, THE Sketch_Project SHALL remove the Original_Sketch file `ai_network_badge_firmware_with_contacts.cpp` from the workspace root.

### Requirement 2: CONFIG Block Contents and Layout

**User Story:** As a person preparing the badge before a conference, I want every user-tunable value grouped in one labeled section at the top of the sketch with comments, so that I can update identity and URLs without reading the rest of the code.

#### Acceptance Criteria

1. THE Sketch_File SHALL contain a CONFIG_Block placed before any non-`#include` code.
2. THE CONFIG_Block SHALL be delimited by clearly labeled comment markers identifying its start and end.
3. THE CONFIG_Block SHALL define `BADGE_OWNER` with the literal value `"Marshall"`.
4. THE CONFIG_Block SHALL define `BADGE_TITLE` with the literal value `"Network & Systems Architect"`.
5. THE CONFIG_Block SHALL define `LINKEDIN_URL` with the literal value `"https://www.linkedin.com/in/marshall-hollis"`.
6. THE CONFIG_Block SHALL define `GITHUB_URL` with the literal value `"https://github.com/slientnight"`.
7. THE CONFIG_Block SHALL define `BLE_BADGE_NAME` with the literal value `"AI-BADGE-MARSHALL"`.
8. THE CONFIG_Block SHALL define `BLE_BADGE_PREFIX` with the literal value `"AI-BADGE"`.
9. THE CONFIG_Block SHALL define `ADMIN_KEY` with the literal value `"meshadmin"`.
10. THE CONFIG_Block SHALL include an inline comment for each of `BADGE_OWNER`, `BADGE_TITLE`, `LINKEDIN_URL`, `GITHUB_URL`, `BLE_BADGE_NAME`, `BLE_BADGE_PREFIX`, and `ADMIN_KEY` describing what the value controls.
11. THE CONFIG_Block SHALL identify which constants are intended to be changed before an event by including a comment block listing them.

### Requirement 3: Wi-Fi SoftAP and Captive Portal Behavior

**User Story:** As an event attendee, I want the badge to expose the same open Wi-Fi network and captive portal as before, so that joining the badge auto-launches the landing page on iOS, Android, and Windows.

#### Acceptance Criteria

1. THE Sketch_File SHALL start a Wi-Fi SoftAP with SSID `AI-BADGE` and no password.
2. THE Sketch_File SHALL configure the SoftAP at IPv4 address `192.168.4.1` with netmask `255.255.255.0`.
3. THE Sketch_File SHALL run a DNSServer on UDP port 53 that resolves all hostnames to `192.168.4.1`.
4. WHEN an HTTP request arrives for any of `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`, `/success.txt`, `/success.html`, `/blank.html`, `/ncsi.txt`, or `/connecttest.txt`, THE Sketch_File SHALL respond with the captive portal landing page using `Cache-Control: no-store, no-cache, must-revalidate, max-age=0` and `Pragma: no-cache` headers.
5. WHEN an HTTP request arrives for `/redirect`, THE Sketch_File SHALL respond with HTTP 302 and a `Location` header set to `http://192.168.4.1`.
6. WHEN an HTTP request arrives for an unregistered path, THE Sketch_File SHALL respond with the captive portal landing page.

### Requirement 4: NeoPixel Hardware Configuration

**User Story:** As the badge builder, I want the LED strip driven on the same pin and protocol as the original firmware, so that existing assembled badges continue to work without rewiring.

#### Acceptance Criteria

1. THE Sketch_File SHALL drive a NeoPixel strip on GPIO `4`.
2. THE Sketch_File SHALL configure the NeoPixel strip with `NUM_LEDS` = 8 pixels.
3. THE Sketch_File SHALL initialize the NeoPixel strip with the `NEO_GRB + NEO_KHZ800` color order and timing.
4. THE Sketch_File SHALL constrain the brightness value to the inclusive range 5 through 120.
5. WHEN a brightness change is requested via the `/brightness` endpoint, THE Sketch_File SHALL clamp the requested value to the 5 through 120 range before applying it.

### Requirement 5: Idle Pattern Rendering

**User Story:** As an attendee viewing the badge, I want the four idle animations preserved exactly, so that the visual identity of the badge is unchanged.

#### Acceptance Criteria

1. THE Sketch_File SHALL implement four Idle_Patterns named `Packet Chase`, `AI Pulse`, `Network Sparkle`, and `Rainbow Mesh`.
2. WHILE no Reaction_Effect is active, THE Sketch_File SHALL render the currently selected Idle_Pattern.
3. THE Sketch_File SHALL advance frames on a 55 ms cadence.
4. WHEN the BOOT button on GPIO 9 transitions from HIGH to LOW with a debounce window of 30 ms, THE Sketch_File SHALL advance to the next Idle_Pattern in cyclic order.
5. WHEN the HTTP endpoint `/next` is requested, THE Sketch_File SHALL advance to the next Idle_Pattern in cyclic order and redirect to `/`.

### Requirement 6: Reaction Effects

**User Story:** As an attendee tapping a button on the badge page, I want the corresponding reaction animation to play for the same duration as before, so that interactions feel identical.

#### Acceptance Criteria

1. THE Sketch_File SHALL implement seven Reaction_Effects named `PACKET`, `LINKUP`, `AI`, `STORM`, `GITHUB`, `LINKEDIN`, and `CONTACT`.
2. WHEN a Reaction_Effect is triggered, THE Sketch_File SHALL render the corresponding animation for `REACTION_MS` = 4500 ms.
3. WHEN `REACTION_MS` milliseconds have elapsed since the reaction started, THE Sketch_File SHALL return to rendering the active Idle_Pattern.
4. WHEN the HTTP endpoint `/trigger` is requested with query parameter `fx` set to one of `packet`, `linkup`, `ai`, `storm`, `github`, or `linkedin`, THE Sketch_File SHALL trigger the matching Reaction_Effect, increment `packetCount` by 1, and redirect to `/`.

### Requirement 7: Build the Mesh Game

**User Story:** As an attendee earning packets, I want the level progression and unlock celebration preserved exactly, so that the gamification experience matches prior demos.

#### Acceptance Criteria

1. THE Sketch_File SHALL track `packetCount` as a non-negative integer.
2. THE Sketch_File SHALL derive the badge level name from `packetCount` using thresholds: 0 = `Offline Node`, 1 = `Listening Node`, 5 = `Linked Node`, 10 = `Mesh Builder`, 25 = `AI Router`, 50 = `Supernode`.
3. WHEN a packet increment causes `packetCount` to cross any of the thresholds 1, 5, 10, 25, or 50, THE Sketch_File SHALL trigger Reaction_Effect `STORM` and set the last signal text to `LEVEL UP: <level name>`.
4. WHEN the HTTP endpoint `/resetcount` is requested, THE Sketch_File SHALL reset `packetCount` and `peerSeenCount` to 0, clear the seen-peer slots, and redirect to `/`.

### Requirement 8: Contact Card Form

**User Story:** As an attendee leaving contact info, I want the same input limits, sanitization, storage limit, and packet bonus as the original firmware, so that the contact flow behaves identically.

#### Acceptance Criteria

1. WHEN an HTTP POST request arrives at `/contact`, THE Sketch_File SHALL read form fields `name`, `contact`, and `note`.
2. THE Sketch_File SHALL sanitize each Contact_Form field by trimming whitespace, replacing carriage return and newline characters with single spaces, collapsing consecutive spaces, and truncating to the field's maximum length.
3. THE Sketch_File SHALL enforce a maximum length of 32 characters for `name`, 80 characters for `contact`, and 120 characters for `note`.
4. IF all three sanitized fields are empty, THEN THE Sketch_File SHALL set the last signal text to `Blank packet card ignored` and redirect to `/` without storing the submission.
5. IF the stored contact count has reached `MAX_CONTACTS` = 25, THEN THE Sketch_File SHALL respond with HTTP status 507 and a plain-text body indicating contact storage is full.
6. WHEN a Contact_Form submission is accepted, THE Sketch_File SHALL persist the sanitized values into NVS namespace `badge` using keys `nm##`, `ct##`, and `nt##` where `##` is a zero-padded two-digit index.
7. WHEN a Contact_Form submission is accepted, THE Sketch_File SHALL trigger Reaction_Effect `CONTACT` and increment `packetCount` by `CONTACT_PACKET_VALUE` = 3.

### Requirement 9: Admin Endpoints

**User Story:** As the badge owner, I want the admin endpoints gated by the same query-key check, so that I can list, export, and clear contacts the same way I always have.

#### Acceptance Criteria

1. WHEN an HTTP GET request arrives at `/contacts`, `/contacts.csv`, or `/clearcontacts` without a query parameter `key` matching `ADMIN_KEY`, THE Sketch_File SHALL respond with HTTP status 403 and a plain-text body instructing the caller to add `?key=YOUR_ADMIN_KEY`.
2. WHEN an HTTP GET request arrives at `/contacts` with `?key=<ADMIN_KEY>`, THE Sketch_File SHALL respond with an HTML page listing stored contacts.
3. WHEN an HTTP GET request arrives at `/contacts.csv` with `?key=<ADMIN_KEY>`, THE Sketch_File SHALL respond with `Content-Type: text/csv` and `Content-Disposition: attachment; filename=badge_contacts.csv`.
4. WHEN an HTTP GET request arrives at `/clearcontacts` with `?key=<ADMIN_KEY>`, THE Sketch_File SHALL remove all `nm##`, `ct##`, and `nt##` keys from NVS namespace `badge`, reset the contact count to 0, and redirect to `/contacts?key=<ADMIN_KEY>`.

### Requirement 10: BLE Presence

**User Story:** As an attendee with another `AI-BADGE`, I want the same BLE discovery and link-up reaction, so that two badges greet each other automatically.

#### Acceptance Criteria

1. THE Sketch_File SHALL initialize the BLE stack with the local advertising name `AI-BADGE-MARSHALL`.
2. THE Sketch_File SHALL run an active BLE scan every 20 seconds for a duration of 3 seconds.
3. WHEN a BLE advertised device with a name starting with `AI-BADGE` is observed, THE Sketch_File SHALL record the peer name if it is not already in the seen-peer list.
4. THE Sketch_File SHALL store at most `MAX_SEEN_PEERS` = 12 peer names in the seen-peer list.
5. WHEN a new peer is recorded, THE Sketch_File SHALL increment `peerSeenCount` and trigger Reaction_Effect `LINKUP`.
6. WHEN the local advertising name is observed during a scan, THE Sketch_File SHALL ignore the result.

### Requirement 11: Persistent Settings

**User Story:** As the badge owner, I want all persistent state to survive a reboot, so that the badge resumes mid-event with its score, brightness, and idle pattern intact.

#### Acceptance Criteria

1. THE Sketch_File SHALL open the NVS_Settings using `Preferences` namespace `badge`.
2. THE Sketch_File SHALL persist `brightness` under NVS key `brightness`.
3. THE Sketch_File SHALL persist `idlePattern` under NVS key `idlePattern`.
4. THE Sketch_File SHALL persist `packetCount` under NVS key `packetCount`.
5. THE Sketch_File SHALL persist the contact count under NVS key `contactCount`.
6. THE Sketch_File SHALL persist `peerSeenCount` under NVS key `peerSeen`.
7. WHEN the Sketch_File starts, THE Sketch_File SHALL load all NVS_Settings before any LED, Wi-Fi, or BLE initialization that depends on those values.

### Requirement 12: README Documentation

**User Story:** As a new contributor cloning the repository, I want a README that explains what the project is, which boards are supported, and how to flash it, so that I can build and run the badge without reading the source.

#### Acceptance Criteria

1. THE Sketch_Project SHALL include a `README.md` file at the workspace root.
2. THE README SHALL include a project overview section describing the badge's purpose and behavior.
3. THE README SHALL list both Seeed XIAO ESP32-C3 and ESP32-C3 SuperMini as supported Target_Boards.
4. THE README SHALL document the Arduino board manager URL for installing the ESP32 board package.
5. THE README SHALL document the Arduino IDE board selection name for each Target_Board.
6. THE README SHALL list the required libraries `Adafruit NeoPixel` and the ESP32 BLE library bundled with the ESP32 core.
7. THE README SHALL include upload instructions covering compiling and flashing through the Arduino IDE or Arduino CLI.
8. THE README SHALL include a first-boot checklist covering joining the `AI-BADGE` Wi-Fi network, opening `http://192.168.4.1`, and accessing the admin endpoint at `http://192.168.4.1/contacts?key=<ADMIN_KEY>`.

### Requirement 13: Wiring Documentation

**User Story:** As a builder soldering a new badge, I want a wiring guide covering both supported boards, so that I can connect the LEDs and button correctly the first time.

#### Acceptance Criteria

1. THE Sketch_Project SHALL include a `wiring.md` file at the workspace root.
2. THE Wiring_Doc SHALL document the LED strip wiring as 8 WS2812 or SK6812 pixels connected to GPIO 4 with power on 5V or 3.3V and a shared ground.
3. THE Wiring_Doc SHALL note that the BOOT button is built into both supported boards on GPIO 9 and does not require external wiring.
4. THE Wiring_Doc SHALL include a per-board pin reference for the Seeed XIAO ESP32-C3 covering the GPIO 4 LED data pin, ground, and power pads.
5. THE Wiring_Doc SHALL include a per-board pin reference for the ESP32-C3 SuperMini covering the GPIO 4 LED data pin, ground, and power pads.
6. THE Wiring_Doc SHALL include power and decoupling guidance covering the recommended capacitor across the LED strip's power rails and the recommended series resistor on the data line.

### Requirement 14: Troubleshooting Documentation

**User Story:** As an attendee or builder hitting a problem at an event, I want a troubleshooting guide that covers the common failure modes, so that I can recover the badge without reading the source.

#### Acceptance Criteria

1. THE Sketch_Project SHALL include a `troubleshooting.md` file at the workspace root.
2. THE Troubleshooting_Doc SHALL include a section covering the captive portal failing to auto-launch, with per-operating-system notes for iOS, Android, and Windows.
3. THE Troubleshooting_Doc SHALL include a section covering the `AI-BADGE` access point not being visible.
4. THE Troubleshooting_Doc SHALL include a section covering the NeoPixel strip not lighting up.
5. THE Troubleshooting_Doc SHALL include a section covering BLE peers not being detected.
6. THE Troubleshooting_Doc SHALL include a section covering full contact storage that references the admin export and clear endpoints.
7. THE Troubleshooting_Doc SHALL include a section covering brightness appearing too dim or too bright that references the brightness clamp range 5 through 120.
8. THE Troubleshooting_Doc SHALL include a section covering the sketch failing to compile due to missing libraries that lists `Adafruit NeoPixel` and the ESP32 BLE library.
9. THE Troubleshooting_Doc SHALL include a section covering upload failures that documents the USB driver requirement and the BOOT-button-held-while-plugging-in workaround for the ESP32-C3 SuperMini.
