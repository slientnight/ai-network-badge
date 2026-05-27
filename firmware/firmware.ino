// =============================================================================
// Section 1: Includes
// =============================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <math.h>
#include "driver/temperature_sensor.h"

// =============================================================================
// CONFIG — Edit values in this block to personalize the badge.
// Everything below this block is implementation and should not need editing.
// =============================================================================

// ---- Change these before taking the badge to an event ----------------------
//   BADGE_OWNER, BADGE_TITLE, LINKEDIN_URL, GITHUB_URL,
//   AP_SSID_OVERRIDE, BLE_BADGE_NAME_OVERRIDE, BLE_BADGE_PREFIX
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
// Leave empty ("") to auto-generate from the chip MAC as AI-BADGE-XXXX.
const char* BLE_BADGE_NAME_OVERRIDE = "";

// Prefix used to detect peer badges during BLE scans. Names starting with
// this prefix (and not equal to the active BLE name) are counted as peers.
const char* BLE_BADGE_PREFIX = "AI-BADGE";

// Local hostname advertised over mDNS / Bonjour. Used as `<host>.local` from
// devices on the badge AP. ESPmDNS expects the bare label without the .local
// suffix.
const char* MDNS_HOSTNAME = "badge";

// Wi-Fi AP SSID. Leave empty ("") to auto-generate as AI-BADGE-XXXX from chip MAC.
const char* AP_SSID_OVERRIDE = "";

// Debug flags — set to 1 to enable, 0 to disable (compiled out entirely).
#define DEBUG_BLE_RSSI 0  // Print peer RSSI to serial on every BLE sighting.

// =============================================================================
// END CONFIG
// =============================================================================

// =============================================================================
// Section 3: Hardware and limits
// =============================================================================
#define LED_PIN 4
#define BOOT_BUTTON 9
#define NUM_LEDS 4   // Badge LED count.

const char* AP_PASS = "";  // Open Wi-Fi network, no password.

const uint8_t MAX_CONTACTS = 25;
const uint8_t CONTACT_PACKET_VALUE = 3;

const unsigned long BLE_SCAN_INTERVAL_MS = 20000;

// How recently a peer must have been seen (via BLE scan) to count as "nearby"
// for the peer-aware idle accent. Two scan intervals plus margin.
const unsigned long PEER_NEARBY_TIMEOUT_MS = 60000;

// How often to re-fetch a peer's score via GATT (avoid hammering connections).
const unsigned long SCORE_FETCH_INTERVAL_MS = 45000;

// BLE GATT UUIDs for the badge leaderboard service.
// Service: exposes the badge's mesh packet count to nearby peers.
#define BADGE_SERVICE_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define BADGE_SCORE_CHAR_UUID     "a1b2c3d4-e5f6-7890-abcd-ef1234567891"
const uint32_t BLE_SCAN_SECONDS = 3;
const uint8_t MAX_SEEN_PEERS = 12;
const unsigned long REACTION_MS = 4500;

const uint8_t ACTIVITY_BUFFER_SIZE = 8;
const uint16_t SERIAL_LINE_MAX = 96;
const uint8_t CONSOLE_LOG_LINES = 24;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);
// =============================================================================
// Section 4: Globals and runtime state
// =============================================================================

struct PeerRecord {
  String name;
  int8_t rssiFirst;
  int8_t rssiLast;
  unsigned long firstSeenMs;
  unsigned long lastSeenMs;
  uint32_t score;          // Peer's packet count (0 = not yet fetched)
  unsigned long scoreFetchedMs;  // When we last read their score
};

enum ActivityCategory {
  ACTIVITY_REACTION,
  ACTIVITY_CONTACT,
  ACTIVITY_PEER,
  ACTIVITY_LEVEL_UP
};

struct ActivityEntry {
  String label;
  unsigned long timestamp;
  ActivityCategory category;
};

Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

uint8_t brightness = 16;
uint8_t idlePattern = 2;
uint32_t packetCount = 0;
uint32_t contactPacketCount = 0;
uint32_t peerSeenCount = 0;

NimBLEScan* bleScan = nullptr;
NimBLECharacteristic* scoreCharacteristic = nullptr;
temperature_sensor_handle_t tempSensor = NULL;
unsigned long lastBleScan = 0;

PeerRecord seenPeers[MAX_SEEN_PEERS];
uint8_t seenPeerSlots = 0;

uint16_t frame = 0;
unsigned long lastFrame = 0;
unsigned long lastButtonCheck = 0;
bool lastButtonState = HIGH;

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

Reaction activeReaction = REACTION_NONE;
unsigned long reactionStart = 0;

String lastSignal = "None yet";

String activeAdminKey = "";
String activeApSsid = "";
String activeBleName = "";
ActivityEntry activityBuffer[ACTIVITY_BUFFER_SIZE];
uint8_t activityHead = 0;
uint8_t activityCount = 0;
String serialLine = "";

String consoleLogBuffer[CONSOLE_LOG_LINES];
uint8_t consoleLogHead = 0;
uint8_t consoleLogCount = 0;

// =============================================================================
// Section 5: Helpers
// =============================================================================

String idlePatternName(uint8_t p) {
  switch (p) {
    case 0: return "Packet Chase";
    case 1: return "AI Pulse";
    case 2: return "Network Sparkle";
    case 3: return "Rainbow Mesh";
    default: return "Packet Chase";
  }
}

String badgeLevelNameForCount(uint32_t count) {
  if (count >= 50) return "Supernode";
  if (count >= 25) return "AI Router";
  if (count >= 10) return "Mesh Builder";
  if (count >= 5) return "Linked Node";
  if (count >= 1) return "Listening Node";
  return "Offline Node";
}

String badgeLevelName() {
  return badgeLevelNameForCount(packetCount);
}

String badgeLevelMeaning() {
  if (packetCount >= 50) return "You became a conference hub.";
  if (packetCount >= 25) return "You are routing high-value packets.";
  if (packetCount >= 10) return "A real social mesh is forming.";
  if (packetCount >= 5) return "You are building strong links.";
  if (packetCount >= 1) return "First connections established.";
  return "Waiting for peers to send packets.";
}

uint32_t nextUnlockCount() {
  if (packetCount < 1) return 1;
  if (packetCount < 5) return 5;
  if (packetCount < 10) return 10;
  if (packetCount < 25) return 25;
  if (packetCount < 50) return 50;
  return 0;
}

String nextUnlockText() {
  uint32_t next = nextUnlockCount();

  if (next == 0) {
    return "All unlocks active";
  }

  uint32_t remaining = next - packetCount;
  if (remaining == 1) {
    return "1 more packet";
  }
  return String(remaining) + " more packets";
}

bool crossedUnlock(uint32_t before, uint32_t after) {
  const uint32_t unlocks[] = {1, 5, 10, 25, 50};
  for (uint8_t i = 0; i < 5; i++) {
    if (before < unlocks[i] && after >= unlocks[i]) {
      return true;
    }
  }
  return false;
}

void saveSettings() {
  prefs.putUChar("brightness", brightness);
  prefs.putUChar("idlePattern", idlePattern);
  prefs.putUInt("packetCount", packetCount);
  prefs.putUInt("contactCount", contactPacketCount);
  prefs.putUInt("peerSeen", peerSeenCount);

  // Keep the GATT characteristic in sync so peers read the latest score.
  if (scoreCharacteristic != nullptr) {
    scoreCharacteristic->setValue(packetCount);
  }
}

void loadSettings() {
  brightness = prefs.getUChar("brightness", 16);
  idlePattern = prefs.getUChar("idlePattern", 2);
  packetCount = prefs.getUInt("packetCount", 0);
  contactPacketCount = prefs.getUInt("contactCount", 0);
  peerSeenCount = prefs.getUInt("peerSeen", 0);
}

String keyFor(const char* prefix, uint8_t index) {
  char key[8];
  snprintf(key, sizeof(key), "%s%02u", prefix, index);
  return String(key);
}

uint32_t wheel(byte pos) {
  pos = 255 - pos;

  if (pos < 85) {
    return pixels.Color(255 - pos * 3, 0, pos * 3);
  }

  if (pos < 170) {
    pos -= 85;
    return pixels.Color(0, pos * 3, 255 - pos * 3);
  }

  pos -= 170;
  return pixels.Color(pos * 3, 255 - pos * 3, 0);
}

void clearPixels() {
  for (int i = 0; i < NUM_LEDS; i++) {
    pixels.setPixelColor(i, 0);
  }
}

String cleanInput(String value, uint16_t maxLen) {
  value.trim();
  value.replace("\r", " ");
  value.replace("\n", " ");

  while (value.indexOf("  ") >= 0) {
    value.replace("  ", " ");
  }

  if (value.length() > maxLen) {
    value = value.substring(0, maxLen);
  }

  return value;
}

String escapeHtml(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

String csvEscape(String value) {
  value.replace("\"", "\"\"");
  return "\"" + value + "\"";
}

String macDerivedAdminKey() {
  // Use the chip's burned-in efuse MAC (always available, factory-fixed) instead
  // of WiFi.macAddress() which depends on the Wi-Fi driver being initialized
  // and was returning unstable values when this was called early in setup().
  // Last 2 bytes -> 4 hex chars; short enough to type from a printed badge,
  // matches the project's low-security threat model (conference badge, not
  // production auth).
  uint8_t mac[8] = {0};
  esp_efuse_mac_get_default(mac);
  char key[5];
  snprintf(key, sizeof(key), "%02x%02x", mac[4], mac[5]);
  return String(key);
}

String macDerivedSuffix() {
  // Same last-2-bytes-of-MAC approach used for the admin key, uppercased for
  // readability in SSIDs and BLE names.
  uint8_t mac[8] = {0};
  esp_efuse_mac_get_default(mac);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  return String(suffix);
}

String resolveApSsid() {
  if (strlen(AP_SSID_OVERRIDE) > 0) return String(AP_SSID_OVERRIDE);
  return "AI-BADGE-" + macDerivedSuffix();
}

String resolveBleName() {
  if (strlen(BLE_BADGE_NAME_OVERRIDE) > 0) return String(BLE_BADGE_NAME_OVERRIDE);
  return "AI-BADGE-" + macDerivedSuffix();
}

String loadActiveAdminKey() {
  String stored = prefs.getString("adminKey", "");
  if (stored.length() > 0) {
    return stored;
  }
  return macDerivedAdminKey();
}

String relativeTimeString(unsigned long pastMs) {
  unsigned long now = millis();
  if (pastMs > now) return "just now";
  unsigned long delta = now - pastMs;
  if (delta < 10000UL) return "just now";
  if (delta < 60000UL) return String((unsigned long)(delta / 1000UL)) + " sec ago";
  if (delta < 3600000UL) return String((unsigned long)(delta / 60000UL)) + " min ago";
  if (delta < 86400000UL) return String((unsigned long)(delta / 3600000UL)) + " hr ago";
  return "24+ hr ago";
}

String rssiLabel(int8_t rssi) {
  if (rssi >= -40) return "Adjacent";
  if (rssi >= -60) return "Near";
  if (rssi >= -80) return "Close";
  if (rssi >= -90) return "Far";
  return "Distant";
}

float readChipTempC() {
  float t = 0.0;
  if (tempSensor != NULL) {
    temperature_sensor_get_celsius(tempSensor, &t);
  }
  return t;
}

float celsiusToFahrenheit(float c) {
  return c * 9.0 / 5.0 + 32.0;
}

void addActivity(ActivityCategory category, String label) {
  activityBuffer[activityHead].label = label;
  activityBuffer[activityHead].timestamp = millis();
  activityBuffer[activityHead].category = category;
  activityHead = (activityHead + 1) % ACTIVITY_BUFFER_SIZE;
  if (activityCount < ACTIVITY_BUFFER_SIZE) {
    activityCount++;
  }
}

String reactionName(Reaction r) {
  switch (r) {
    case REACTION_PACKET:   return "Packet";
    case REACTION_LINKUP:   return "Link";
    case REACTION_AI:       return "AI inference";
    case REACTION_STORM:    return "Storm";
    case REACTION_GITHUB:   return "GitHub";
    case REACTION_LINKEDIN: return "LinkedIn";
    case REACTION_CONTACT:  return "Contact";
    default:                return "Reaction";
  }
}

void factoryResetAndReboot() {
  Serial.println("Factory reset: clearing NVS namespace 'badge' and rebooting...");
  prefs.clear();
  prefs.end();
  delay(200);
  ESP.restart();
}

// Single output choke point used by both the USB serial command handler and
// the web console. Writes to the hardware Serial and also to a small ring
// buffer that the web console reads back.
void consoleLog(String line) {
  Serial.println(line);
  consoleLogBuffer[consoleLogHead] = line;
  consoleLogHead = (consoleLogHead + 1) % CONSOLE_LOG_LINES;
  if (consoleLogCount < CONSOLE_LOG_LINES) {
    consoleLogCount++;
  }
}

void processSerialLine(String line) {
  line.trim();
  if (line.startsWith("setkey=")) {
    String value = line.substring(7);
    value.trim();
    if (value.length() == 0) {
      consoleLog("setkey= requires a non-empty value");
      return;
    }
    prefs.putString("adminKey", value);
    activeAdminKey = value;
    consoleLog("Admin key set: " + activeAdminKey);
  } else if (line == "clearkey") {
    prefs.remove("adminKey");
    activeAdminKey = macDerivedAdminKey();
    consoleLog("Admin key cleared. Active key: " + activeAdminKey);
  } else if (line == "factoryreset") {
    factoryResetAndReboot();
  } else if (line.length() > 0) {
    consoleLog("Unknown command: " + line);
  }
}

bool adminAllowed() {
  return server.hasArg("key") && server.arg("key") == activeAdminKey;
}

bool peerAlreadySeen(String peerName) {
  for (uint8_t i = 0; i < seenPeerSlots; i++) {
    if (seenPeers[i].name == peerName) {
      return true;
    }
  }
  return false;
}

void rememberPeer(String peerName, int8_t rssi) {
  if (peerName.length() == 0) return;
  if (peerName == activeBleName) return;
  if (!peerName.startsWith(BLE_BADGE_PREFIX)) return;

  // Repeat sighting — update rssiLast and lastSeenMs only.
  for (uint8_t i = 0; i < seenPeerSlots; i++) {
    if (seenPeers[i].name == peerName) {
      seenPeers[i].rssiLast = rssi;
      seenPeers[i].lastSeenMs = millis();
#if DEBUG_BLE_RSSI
      Serial.print("[BLE] ");
      Serial.print(peerName);
      Serial.print(" rssi=");
      Serial.println(rssi);
#endif
      return;
    }
  }

  // First sighting — populate a fresh PeerRecord.
  uint8_t idx;
  if (seenPeerSlots < MAX_SEEN_PEERS) {
    idx = seenPeerSlots++;
  } else {
    // Simple ring-ish behavior: overwrite slot 0 if the demo sees many badges.
    idx = 0;
  }
  seenPeers[idx].name = peerName;
  seenPeers[idx].rssiFirst = rssi;
  seenPeers[idx].rssiLast = rssi;
  seenPeers[idx].firstSeenMs = millis();
  seenPeers[idx].lastSeenMs = millis();
  seenPeers[idx].score = 0;
  seenPeers[idx].scoreFetchedMs = 0;

  peerSeenCount++;
  lastSignal = "Peer found: " + peerName;
#if DEBUG_BLE_RSSI
  Serial.print("[BLE] NEW ");
  Serial.print(peerName);
  Serial.print(" rssi=");
  Serial.println(rssi);
#endif
  saveSettings();

  activeReaction = REACTION_LINKUP;
  reactionStart = millis();

  addActivity(ACTIVITY_PEER, "Peer: " + peerName);
}

class BadgeAdvertisedDeviceCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    if (!advertisedDevice->haveName()) {
      return;
    }

    String peerName = String(advertisedDevice->getName().c_str());
    rememberPeer(peerName, (int8_t)advertisedDevice->getRSSI());
  }
};

void startBlePresence() {
  NimBLEDevice::init(activeBleName.c_str());

  // GATT server: expose our packet count so nearby badges can read it.
  NimBLEServer* pServer = NimBLEDevice::createServer();
  NimBLEService* pService = pServer->createService(BADGE_SERVICE_UUID);
  scoreCharacteristic = pService->createCharacteristic(
    BADGE_SCORE_CHAR_UUID,
    NIMBLE_PROPERTY::READ
  );
  // Set initial value (4 bytes, little-endian uint32).
  scoreCharacteristic->setValue(packetCount);
  pService->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(activeBleName.c_str());
  advertising->addServiceUUID(BADGE_SERVICE_UUID);
  advertising->enableScanResponse(true);
  NimBLEDevice::startAdvertising();

  bleScan = NimBLEDevice::getScan();
  bleScan->setScanCallbacks(new BadgeAdvertisedDeviceCallbacks(), true);
  bleScan->setActiveScan(true);
  bleScan->setInterval(160);
  bleScan->setWindow(80);
}

void runBlePresenceScan() {
  if (bleScan == nullptr) return;
  if (millis() - lastBleScan < BLE_SCAN_INTERVAL_MS) return;

  lastBleScan = millis();
  bleScan->start(BLE_SCAN_SECONDS * 1000, false);
  // Don't clear results here — fetchPeerScore() needs them to find addresses.
  NimBLEDevice::startAdvertising();
}

// Attempt to connect to a peer badge and read its packet count via GATT.
// Called from the main loop for one peer at a time to avoid blocking too long.
void fetchPeerScore(uint8_t peerIdx) {
  if (peerIdx >= seenPeerSlots) return;

  PeerRecord& peer = seenPeers[peerIdx];

  // Skip if we fetched recently.
  if (peer.scoreFetchedMs > 0 && millis() - peer.scoreFetchedMs < SCORE_FETCH_INTERVAL_MS) return;

  // Skip if peer hasn't been seen recently (probably out of range).
  if (millis() - peer.lastSeenMs > PEER_NEARBY_TIMEOUT_MS) return;

  // We need the peer's BLE address. NimBLE scan results are cleared, so we
  // attempt a fresh scan result lookup. If unavailable, skip this cycle.
  NimBLEAdvertisedDevice* device = nullptr;

  // Do a quick scan to find the peer's address.
  NimBLEScanResults results = bleScan->getResults();
#if DEBUG_BLE_RSSI
  Serial.print("[LEADERBOARD] Looking for ");
  Serial.print(peer.name);
  Serial.print(" in ");
  Serial.print(results.getCount());
  Serial.println(" scan results");
#endif
  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    if (d->haveName() && String(d->getName().c_str()) == peer.name) {
      device = const_cast<NimBLEAdvertisedDevice*>(d);
      break;
    }
  }

  // If we can't find the device address, skip this cycle.
  if (device == nullptr) {
#if DEBUG_BLE_RSSI
    Serial.println("[LEADERBOARD] Peer not in scan results, skipping");
#endif
    return;
  }

#if DEBUG_BLE_RSSI
  Serial.print("[LEADERBOARD] Connecting to ");
  Serial.println(peer.name);
#endif

  // Stop scanning before connecting — NimBLE can't scan and connect simultaneously.
  bleScan->stop();

  NimBLEClient* pClient = NimBLEDevice::createClient();
  pClient->setConnectTimeout(10);  // 10 seconds — peers are often busy scanning.

  if (!pClient->connect(device)) {
#if DEBUG_BLE_RSSI
    Serial.println("[LEADERBOARD] Connection failed");
#endif
    NimBLEDevice::deleteClient(pClient);
    return;
  }

  NimBLERemoteService* pService = pClient->getService(BADGE_SERVICE_UUID);
  if (pService != nullptr) {
    NimBLERemoteCharacteristic* pChar = pService->getCharacteristic(BADGE_SCORE_CHAR_UUID);
    if (pChar != nullptr && pChar->canRead()) {
      uint32_t remoteScore = pChar->getValue<uint32_t>();
      peer.score = remoteScore;
      peer.scoreFetchedMs = millis();
#if DEBUG_BLE_RSSI
      Serial.print("[LEADERBOARD] Got score=");
      Serial.print(remoteScore);
      Serial.print(" from ");
      Serial.println(peer.name);
#endif
    }
  } else {
#if DEBUG_BLE_RSSI
    Serial.println("[LEADERBOARD] Service not found on peer");
#endif
  }

  pClient->disconnect();
  NimBLEDevice::deleteClient(pClient);

  // Resume advertising after the client connection.
  NimBLEDevice::startAdvertising();
}

// Called from loop() — fetches one peer's score per cycle to avoid blocking.
void runLeaderboardSync() {
  static uint8_t nextPeerToFetch = 0;
  static unsigned long lastFetchAttempt = 0;

  // Only attempt a fetch every 10 seconds to avoid thrashing BLE.
  if (millis() - lastFetchAttempt < 10000) return;
  if (seenPeerSlots == 0) return;

  lastFetchAttempt = millis();

#if DEBUG_BLE_RSSI
  Serial.print("[LEADERBOARD] Attempting fetch for peer slot ");
  Serial.println(nextPeerToFetch);
#endif

  // Round-robin through peers.
  if (nextPeerToFetch >= seenPeerSlots) nextPeerToFetch = 0;
  fetchPeerScore(nextPeerToFetch);
  nextPeerToFetch++;
}

// =============================================================================
// Section 6: Idle patterns
// =============================================================================

void idlePacketChase() {
  clearPixels();

  int head = frame % NUM_LEDS;
  int tail1 = (head - 1 + NUM_LEDS) % NUM_LEDS;
  int tail2 = (head - 2 + NUM_LEDS) % NUM_LEDS;

  pixels.setPixelColor(head, pixels.Color(0, 180, 255));
  pixels.setPixelColor(tail1, pixels.Color(0, 70, 130));
  pixels.setPixelColor(tail2, pixels.Color(0, 20, 45));
}

void idleAIPulse() {
  clearPixels();

  float s = sin(frame * 0.10);
  uint8_t pulse = (uint8_t)((s + 1.0) * 80.0 + 20.0);

  for (int i = 0; i < NUM_LEDS; i++) {
    int distanceFromCenter = abs(i - (NUM_LEDS / 2));
    int level = pulse - distanceFromCenter * 18;

    if (level < 3) level = 3;
    if (level > 255) level = 255;

    pixels.setPixelColor(i, pixels.Color(level / 2, 0, level));
  }
}

void idleSparkle() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint32_t current = pixels.getPixelColor(i);

    uint8_t r = (current >> 16) & 0xFF;
    uint8_t g = (current >> 8) & 0xFF;
    uint8_t b = current & 0xFF;

    r = r > 8 ? r - 8 : 0;
    g = g > 8 ? g - 8 : 0;
    b = b > 8 ? b - 8 : 0;

    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }

  if (random(0, 5) == 0) {
    int idx = random(NUM_LEDS);
    pixels.setPixelColor(idx, pixels.Color(0, 255, 120));
  }
}

void idleRainbowMesh() {
  for (int i = 0; i < NUM_LEDS; i++) {
    pixels.setPixelColor(i, wheel((i * 256 / NUM_LEDS + frame * 3) & 255));
  }
}

bool peersNearby() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < seenPeerSlots; i++) {
    if (now - seenPeers[i].lastSeenMs < PEER_NEARBY_TIMEOUT_MS) {
      return true;
    }
  }
  return false;
}

void renderIdle() {
  switch (idlePattern) {
    case 0:
      idlePacketChase();
      break;
    case 1:
      idleAIPulse();
      break;
    case 2:
      idleSparkle();
      break;
    case 3:
      idleRainbowMesh();
      break;
    default:
      idlePattern = 0;
      idlePacketChase();
      break;
  }

  // Peer-aware accent: when at least one BLE peer is nearby, overlay a gentle
  // green pulse on LED 0 so the badge owner has a passive "you're not alone"
  // indicator without needing to check the web page.
  if (peersNearby()) {
    float pulse = (sin(frame * 0.08) + 1.0) * 0.5;  // 0.0 – 1.0
    uint8_t g = (uint8_t)(40 + pulse * 60);          // 40 – 100 green
    uint32_t current = pixels.getPixelColor(0);
    uint8_t r = (current >> 16) & 0xFF;
    uint8_t b = current & 0xFF;
    // Blend: keep existing red/blue, boost green channel
    uint8_t existingG = (current >> 8) & 0xFF;
    uint8_t blendedG = existingG > g ? existingG : g;
    pixels.setPixelColor(0, pixels.Color(r, blendedG, b));
  }
}

// =============================================================================
// Section 7: Reaction effects
// =============================================================================

void reactionPacket(unsigned long elapsed) {
  clearPixels();

  int step = (elapsed / 90) % NUM_LEDS;
  pixels.setPixelColor(step, pixels.Color(0, 200, 255));

  int prev = (step - 1 + NUM_LEDS) % NUM_LEDS;
  pixels.setPixelColor(prev, pixels.Color(0, 60, 120));
}

void reactionLinkUp(unsigned long elapsed) {
  clearPixels();

  int lit = map(elapsed, 0, REACTION_MS, 0, NUM_LEDS + 1);
  if (lit > NUM_LEDS) lit = NUM_LEDS;

  for (int i = 0; i < lit; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 220, 70));
  }
}

void reactionAI(unsigned long elapsed) {
  clearPixels();

  float pulse = (sin(elapsed * 0.015) + 1.0) * 0.5;

  for (int i = 0; i < NUM_LEDS; i++) {
    int center = NUM_LEDS / 2;
    int distance = abs(i - center);
    int level = 180 * pulse - distance * 18;

    if (level < 8) level = 8;

    pixels.setPixelColor(i, pixels.Color(level / 2, 0, level));
  }
}

void reactionStorm(unsigned long elapsed) {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (random(0, 3) == 0) {
      pixels.setPixelColor(i, wheel(random(255)));
    } else {
      pixels.setPixelColor(i, 0);
    }
  }
}

void reactionGithub(unsigned long elapsed) {
  clearPixels();

  for (int i = 0; i < NUM_LEDS; i++) {
    bool on = ((elapsed / 120 + i) % 3) == 0;

    if (on) {
      pixels.setPixelColor(i, pixels.Color(230, 230, 230));
    } else {
      pixels.setPixelColor(i, pixels.Color(0, 120, 40));
    }
  }
}

void reactionLinkedIn(unsigned long elapsed) {
  clearPixels();

  uint8_t level = (sin(elapsed * 0.012) + 1.0) * 80 + 30;

  for (int i = 0; i < NUM_LEDS; i++) {
    pixels.setPixelColor(i, pixels.Color(0, level / 2, level));
  }
}

void reactionContact(unsigned long elapsed) {
  clearPixels();

  uint8_t wave = (elapsed / 85) % (NUM_LEDS + 4);
  for (int i = 0; i < NUM_LEDS; i++) {
    int distance = abs(i - wave);
    int level = 220 - distance * 55;
    if (level < 0) level = 0;

    // Gold/green contact-card pulse.
    pixels.setPixelColor(i, pixels.Color(level, level / 2, level / 8));
  }
}

void renderReaction() {
  unsigned long elapsed = millis() - reactionStart;

  if (elapsed > REACTION_MS) {
    activeReaction = REACTION_NONE;
    return;
  }

  switch (activeReaction) {
    case REACTION_PACKET:
      reactionPacket(elapsed);
      break;
    case REACTION_LINKUP:
      reactionLinkUp(elapsed);
      break;
    case REACTION_AI:
      reactionAI(elapsed);
      break;
    case REACTION_STORM:
      reactionStorm(elapsed);
      break;
    case REACTION_GITHUB:
      reactionGithub(elapsed);
      break;
    case REACTION_LINKEDIN:
      reactionLinkedIn(elapsed);
      break;
    case REACTION_CONTACT:
      reactionContact(elapsed);
      break;
    default:
      break;
  }
}

void triggerReaction(Reaction r, String signalName, uint8_t packetValue) {
  activeReaction = r;
  reactionStart = millis();
  lastSignal = signalName;

  if (r != REACTION_NONE) {
    addActivity(ACTIVITY_REACTION, reactionName(r));
  }

  if (packetValue > 0) {
    uint32_t before = packetCount;
    packetCount += packetValue;

    if (crossedUnlock(before, packetCount)) {
      activeReaction = REACTION_STORM;
      reactionStart = millis();
      lastSignal = "LEVEL UP: " + badgeLevelName();
      addActivity(ACTIVITY_LEVEL_UP, "Level up: " + badgeLevelName());
    }
  }

  saveSettings();
}

// =============================================================================
// Section 8: HTML page builders
// =============================================================================

String htmlPage() {
  String html = "";

  html += "<!doctype html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>I Network With AI</title>";

  html += "<style>";
  html += ":root{color-scheme:dark;}";
  html += "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;";
  html += "background:radial-gradient(circle at top,#22314a,#090d14 70%);";
  html += "color:white;margin:0;padding:22px;}";
  html += ".card{max-width:560px;margin:auto;background:#121a27ee;";
  html += "border:1px solid #33435c;border-radius:24px;padding:24px;";
  html += "box-shadow:0 18px 60px #0009;}";
  html += "h1{font-size:38px;line-height:1.02;margin:0 0 8px;letter-spacing:.02em;}";
  html += "h2{font-size:18px;margin-top:26px;color:#d9e6ff;}";
  html += "p{color:#bec8d8;line-height:1.45;}";
  html += ".game,.formbox{background:#0d1420;border:1px solid #33435c;border-radius:18px;padding:16px;margin:18px 0;}";
  html += ".level{font-size:24px;font-weight:900;color:#7dffca;margin:4px 0;}";
  html += ".pill{display:inline-block;background:#26344a;border:1px solid #3a4e6c;";
  html += "border-radius:999px;padding:8px 12px;margin:4px 6px 4px 0;color:#dbe8ff;}";
  html += "a,button{display:block;width:100%;box-sizing:border-box;margin:10px 0;";
  html += "padding:15px;border-radius:14px;text-align:center;text-decoration:none;";
  html += "font-weight:800;font-size:16px;}";
  html += "a{background:#2f80ed;color:white;}";
  html += "button{border:0;background:#2ee58f;color:#06120b;cursor:pointer;}";
  html += ".secondary{background:#26344a;color:#e8f0ff;}";
  html += ".danger{background:#70404a;color:#fff;}";
  html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}";
  html += ".grid button{margin:0;}";
  html += "label{display:block;font-weight:800;margin:12px 0 6px;color:#d9e6ff;}";
  html += "input,textarea{width:100%;box-sizing:border-box;border:1px solid #3a4e6c;background:#172235;color:white;";
  html += "border-radius:12px;padding:12px;font:inherit;}";
  html += "textarea{min-height:76px;resize:vertical;}";
  html += "table{width:100%;border-collapse:collapse;margin-top:10px;font-size:14px;}";
  html += "td,th{border-bottom:1px solid #33435c;padding:8px;text-align:left;}";
  html += "th{color:#7dffca;}";
  html += ".small{font-size:13px;color:#91a0b7;}";
  html += "</style></head><body>";

  html += "<div class='card'>";
  html += "<h1>I NETWORK<br>WITH AI</h1>";

  html += "<p>Hi, I’m <b>" + String(BADGE_OWNER) + "</b>.<br>";
  html += String(BADGE_TITLE) + " exploring AI-assisted infrastructure.</p>";

  html += "<div class='game'>";
  html += "<h2>BUILD THE MESH</h2>";
  html += "<div class='level'>" + badgeLevelName() + "</div>";
  html += "<p>" + badgeLevelMeaning() + "</p>";
  html += "<span class='pill'>Mesh packets: " + String(packetCount) + "</span>";
  html += "<span class='pill'>Packet cards: " + String(contactPacketCount) + "</span>";
  html += "<span class='pill'>Peers found: " + String(peerSeenCount) + "</span>";
  html += "<span class='pill'>Next unlock: " + nextUnlockText() + "</span>";
  html += "<span class='pill'>Last signal: " + escapeHtml(lastSignal) + "</span>";
  html += "</div>";

  html += "<h2>Connect</h2>";
  html += "<a href='" + String(LINKEDIN_URL) + "' target='_blank'>Connect on LinkedIn</a>";
  html += "<a href='" + String(GITHUB_URL) + "' target='_blank'>View GitHub</a>";

  html += "<h2>Nearby Badges</h2>";
  html += "<div class='game'>";
  if (seenPeerSlots == 0) {
    html += "<p class='small'>No peers seen yet.</p>";
  } else {
    uint8_t activeCount = 0;
    uint8_t staleCount = 0;
    // Show active peers first (seen within timeout).
    for (uint8_t i = 0; i < seenPeerSlots; i++) {
      if (millis() - seenPeers[i].lastSeenMs < PEER_NEARBY_TIMEOUT_MS) {
        html += "<p><b>" + escapeHtml(seenPeers[i].name) + "</b> ";
        html += "<span class='pill'>" + rssiLabel(seenPeers[i].rssiLast) + " (" + String(seenPeers[i].rssiLast) + " dBm)</span> ";
        html += "<span class='small'>" + relativeTimeString(seenPeers[i].lastSeenMs) + "</span></p>";
        activeCount++;
      } else {
        staleCount++;
      }
    }
    if (activeCount == 0) {
      html += "<p class='small'>No peers in range right now.</p>";
    }
    // Show stale peers dimmed.
    if (staleCount > 0) {
      html += "<p class='small' style='margin-top:12px;color:#667'>Previously seen:</p>";
      for (uint8_t i = 0; i < seenPeerSlots; i++) {
        if (millis() - seenPeers[i].lastSeenMs >= PEER_NEARBY_TIMEOUT_MS) {
          html += "<p style='opacity:0.5'><b>" + escapeHtml(seenPeers[i].name) + "</b> ";
          html += "<span class='small'>last seen " + relativeTimeString(seenPeers[i].lastSeenMs) + "</span></p>";
        }
      }
    }
  }
  html += "</div>";

  // Mesh Leaderboard: show this badge + nearby peers ranked by packet count.
  html += "<h2>Mesh Leaderboard</h2>";
  html += "<div class='game'>";

  // Build a simple sorted list of entries (this badge + peers with known scores).
  // Using a lightweight insertion approach since MAX_SEEN_PEERS is small (12).
  struct LeaderEntry { String name; uint32_t score; bool isLocal; };
  LeaderEntry entries[MAX_SEEN_PEERS + 1];
  uint8_t entryCount = 0;

  // Add local badge.
  entries[entryCount++] = { String(BADGE_OWNER) + " (you)", packetCount, true };

  // Add peers that have a known score and were seen recently.
  for (uint8_t i = 0; i < seenPeerSlots; i++) {
    if (seenPeers[i].score > 0 && millis() - seenPeers[i].lastSeenMs < PEER_NEARBY_TIMEOUT_MS) {
      entries[entryCount++] = { seenPeers[i].name, seenPeers[i].score, false };
    }
  }

  // Simple insertion sort descending by score.
  for (uint8_t i = 1; i < entryCount; i++) {
    LeaderEntry temp = entries[i];
    int j = i - 1;
    while (j >= 0 && entries[j].score < temp.score) {
      entries[j + 1] = entries[j];
      j--;
    }
    entries[j + 1] = temp;
  }

  if (entryCount <= 1) {
    html += "<p class='small'>Waiting for nearby badges to sync scores...</p>";
  } else {
    html += "<table><tr><th>#</th><th>Badge</th><th>Packets</th><th>Level</th></tr>";
    for (uint8_t i = 0; i < entryCount; i++) {
      html += "<tr";
      if (entries[i].isLocal) html += " style='color:#7dffca;font-weight:bold'";
      html += "><td>" + String(i + 1) + "</td>";
      html += "<td>" + escapeHtml(entries[i].name) + "</td>";
      html += "<td>" + String(entries[i].score) + "</td>";
      html += "<td>" + badgeLevelNameForCount(entries[i].score) + "</td></tr>";
    }
    html += "</table>";
  }
  html += "</div>";

  html += "<h2>Recent Activity</h2>";
  html += "<div class='game'>";
  if (activityCount == 0) {
    html += "<p class='small'>No activity yet.</p>";
  } else {
    for (uint8_t i = 0; i < activityCount; i++) {
      uint8_t idx = (activityHead + ACTIVITY_BUFFER_SIZE - 1 - i) % ACTIVITY_BUFFER_SIZE;
      html += "<p>" + escapeHtml(activityBuffer[idx].label) + " ";
      html += "<span class='small'>" + relativeTimeString(activityBuffer[idx].timestamp) + "</span></p>";
    }
  }
  html += "</div>";

  html += "<h2>Send a packet</h2>";
  html += "<div class='grid'>";
  html += "<form action='/trigger' method='get'><button name='fx' value='packet'>Send Packet</button></form>";
  html += "<form action='/trigger' method='get'><button name='fx' value='linkup'>Establish Link</button></form>";
  html += "<form action='/trigger' method='get'><button name='fx' value='ai'>Run AI Inference</button></form>";
  html += "<form action='/trigger' method='get'><button name='fx' value='storm'>Network Storm</button></form>";
  html += "<form action='/trigger' method='get'><button name='fx' value='github'>Fork on GitHub</button></form>";
  html += "<form action='/trigger' method='get'><button name='fx' value='linkedin'>Connect on LinkedIn</button></form>";
  html += "</div>";

  html += "<div class='formbox'>";
  html += "<h2>Leave a Packet Card</h2>";
  html += "<p>Want to connect after this? Leave your contact info. It is stored locally on this badge and is not uploaded to the internet.</p>";
  html += "<form action='/contact' method='post'>";
  html += "<label for='name'>Name</label>";
  html += "<input id='name' name='name' maxlength='32' placeholder='Your name'>";
  html += "<label for='contact'>Email, LinkedIn, GitHub, or website</label>";
  html += "<input id='contact' name='contact' maxlength='80' placeholder='How should I reach you?'>";
  html += "<label for='note'>Note</label>";
  html += "<textarea id='note' name='note' maxlength='120' placeholder='Where did we meet / what should I remember?'></textarea>";
  html += "<button type='submit'>Send Contact Packet (+3)</button>";
  html += "</form>";
  html += "</div>";

  html += "<h2>Badge controls</h2>";
  html += "<span class='pill'>Idle: " + idlePatternName(idlePattern) + "</span>";
  html += "<a class='secondary' href='/next'>Next idle pattern</a>";

  html += "<form action='/brightness' method='get'>";
  html += "<p>Brightness: " + String(brightness) + " / 255</p>";
  html += "<input type='range' name='b' min='5' max='120' value='" + String(brightness) + "' onchange='this.form.submit()'>";
  html += "</form>";

  html += "<a class='danger' href='/resetcount'>Reset mesh score</a>";
  html += "<a class='secondary' href='/admin'>Owner: View Contacts</a>";
  html += "<a class='secondary' href='/admin?next=/console'>Owner: Console</a>";

  html += "<p class='small'>BLE presence: <b>" + activeBleName + "</b>. Nearby AI badges count as peers.</p>";
  float chipTemp = readChipTempC();
  html += "<p class='small'>CPU temp: <b>" + String(celsiusToFahrenheit(chipTemp), 1) + " &deg;F</b> (" + String(chipTemp, 1) + " &deg;C)</p>";
  html += "<p class='small'>Admin: <b>/contacts?key=YOUR_KEY</b> and <b>/contacts.csv?key=YOUR_KEY</b></p>";
  html += "<p class='small'>Wi-Fi: <b>" + activeApSsid + "</b><br>";
  html += "Open network, no password required.<br>";
  html += "This page should open automatically after joining Wi-Fi.<br>";
  html += "If not, open: <b>http://192.168.4.1</b> or <b>http://" + String(MDNS_HOSTNAME) + ".local</b></p>";

  html += "</div></body></html>";

  return html;
}

String contactSuccessPage(String name) {
  String html = "";

  html += "<!doctype html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Packet Card Received</title>";
  html += "<style>body{font-family:system-ui;background:#090d14;color:white;margin:0;padding:22px;}";
  html += ".card{max-width:520px;margin:auto;background:#121a27;border:1px solid #33435c;border-radius:24px;padding:24px;}";
  html += "h1{font-size:34px;line-height:1.05}.level{color:#7dffca;font-size:24px;font-weight:900}";
  html += "a{display:block;background:#2ee58f;color:#06120b;text-align:center;text-decoration:none;font-weight:900;padding:15px;border-radius:14px;margin-top:18px}";
  html += "p{color:#bec8d8;line-height:1.45}</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>Contact Packet Received</h1>";
  html += "<p>Thanks";
  if (name.length() > 0) {
    html += ", <b>" + escapeHtml(name) + "</b>";
  }
  html += ". You added <b>+" + String(CONTACT_PACKET_VALUE) + "</b> mesh packets.</p>";
  html += "<div class='level'>" + badgeLevelName() + "</div>";
  html += "<p>Mesh packets: " + String(packetCount) + "<br>Packet cards: " + String(contactPacketCount) + "</p>";
  html += "<a href='/'>Back to badge</a>";
  html += "</div></body></html>";

  return html;
}

String adminLoginPage(bool wrongKey, String nextPath) {
  String html = "";
  html += "<!doctype html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Owner Login</title>";
  html += "<style>";
  html += ":root{color-scheme:dark;}";
  html += "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;";
  html += "background:radial-gradient(circle at top,#22314a,#090d14 70%);";
  html += "color:white;margin:0;padding:22px;min-height:100vh;}";
  html += ".card{max-width:420px;margin:60px auto;background:#121a27ee;";
  html += "border:1px solid #33435c;border-radius:24px;padding:28px;";
  html += "box-shadow:0 18px 60px #0009;}";
  html += "h1{font-size:26px;margin:0 0 6px;}";
  html += "p{color:#bec8d8;line-height:1.45;margin:6px 0 16px;}";
  html += "label{display:block;font-weight:800;margin:14px 0 6px;color:#d9e6ff;}";
  html += "input{width:100%;box-sizing:border-box;border:1px solid #3a4e6c;";
  html += "background:#172235;color:white;border-radius:12px;padding:12px;font:inherit;}";
  html += "button{display:block;width:100%;box-sizing:border-box;margin-top:16px;";
  html += "padding:15px;border-radius:14px;text-align:center;border:0;";
  html += "background:#2ee58f;color:#06120b;font-weight:800;font-size:16px;cursor:pointer;}";
  html += "a{display:block;text-align:center;color:#bec8d8;text-decoration:none;margin-top:14px;}";
  html += ".error{background:#5a2230;border:1px solid #ff7a90;color:#ffd6dc;";
  html += "border-radius:12px;padding:10px 12px;margin:0 0 12px;font-size:14px;}";
  html += ".small{font-size:13px;color:#91a0b7;margin-top:18px;}";
  html += "</style></head><body>";

  html += "<div class='card'>";
  html += "<h1>Owner Login</h1>";
  html += "<p>Enter the badge admin key to view stored contacts.</p>";
  if (wrongKey) {
    html += "<div class='error'>Wrong key. Try again.</div>";
  }
  html += "<form action='" + nextPath + "' method='get'>";
  html += "<label for='key'>Admin key</label>";
  html += "<input id='key' name='key' type='password' autocomplete='off' autofocus>";
  html += "<button type='submit'>View Contacts</button>";
  html += "</form>";
  html += "<p class='small'>Hold the BOOT button on the badge and visit <b>/admin/key</b> if you forgot the key.</p>";
  html += "<a href='/'>&larr; Back to badge</a>";
  html += "</div></body></html>";

  return html;
}

String adminContactsPage() {
  String html = "";

  html += "<!doctype html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Badge Contacts</title>";
  html += "<style>body{font-family:system-ui;background:#090d14;color:white;margin:0;padding:22px;}";
  html += ".card{max-width:760px;margin:auto;background:#121a27;border:1px solid #33435c;border-radius:24px;padding:24px;}";
  html += "table{width:100%;border-collapse:collapse;margin-top:18px}td,th{border-bottom:1px solid #33435c;padding:10px;text-align:left;vertical-align:top}th{color:#7dffca}";
  html += "a{display:inline-block;background:#2f80ed;color:white;text-decoration:none;font-weight:900;padding:12px 14px;border-radius:12px;margin:6px 6px 6px 0}.danger{background:#70404a}";
  html += "p{color:#bec8d8}</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>Badge Contacts</h1>";
  html += "<p>Stored locally on this badge. Contacts: <b>" + String(contactPacketCount) + "</b> / " + String(MAX_CONTACTS) + "</p>";
  html += "<a href='/contacts.csv?key=" + activeAdminKey + "'>Download CSV</a>";
  html += "<a class='danger' href='/clearcontacts?key=" + activeAdminKey + "'>Clear contacts</a>";
  html += "<a href='/console?key=" + activeAdminKey + "'>Console</a>";
  html += "<a href='/'>Back to badge</a>";

  html += "<table><tr><th>#</th><th>Name</th><th>Contact</th><th>Note</th></tr>";
  for (uint8_t i = 0; i < contactPacketCount && i < MAX_CONTACTS; i++) {
    String name = prefs.getString(keyFor("nm", i).c_str(), "");
    String contact = prefs.getString(keyFor("ct", i).c_str(), "");
    String note = prefs.getString(keyFor("nt", i).c_str(), "");

    html += "<tr><td>" + String(i + 1) + "</td><td>" + escapeHtml(name) + "</td><td>" + escapeHtml(contact) + "</td><td>" + escapeHtml(note) + "</td></tr>";
  }
  html += "</table>";
  html += "</div></body></html>";

  return html;
}

String captivePortalLandingPage() {
  String html = "";

  html += "<!doctype html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>AI Network Badge</title>";
  html += "</head><body style='font-family:system-ui;margin:0;padding:24px;background:#090d14;color:#fff'>";
  html += "<h1 style='margin:0 0 8px;font-size:30px'>I Network With AI</h1>";
  html += "<p style='color:#bec8d8;margin:0 0 18px'>Tap below to open the badge.</p>";
  html += "<a href='http://192.168.4.1/' style='display:block;text-align:center;background:#2ee58f;color:#06120b;text-decoration:none;font-weight:900;padding:16px;border-radius:12px'>Open Badge Page</a>";
  html += "</body></html>";

  return html;
}

// =============================================================================
// Section 9: HTTP handlers
// =============================================================================

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleCaptivePortalProbe() {
  // Captive-network probes from iOS, Android, and Windows expect either an
  // open-internet success body OR a redirect that signals "captive". A 302 to
  // the badge IP is the fastest way to tell the OS the network is captive
  // (no body to download, OS pops the portal immediately).
  server.sendHeader("Location", String("http://") + apIP.toString() + "/", true);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Connection", "close");
  server.send(302, "text/plain", "");
}

void handleNotFound() {
  // For everything else (typed URLs, hostname guesses), return a tiny landing
  // page rather than a redirect so the captive browser shows our content.
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", captivePortalLandingPage());
}

void redirectToPortal() {
  server.sendHeader("Location", String("http://") + apIP.toString(), true);
  server.send(302, "text/plain", "");
}

void handleTrigger() {
  String fx = server.hasArg("fx") ? server.arg("fx") : "";

  if (fx == "packet") {
    triggerReaction(REACTION_PACKET, "Packet received", 1);
  } else if (fx == "linkup") {
    triggerReaction(REACTION_LINKUP, "Link established", 1);
  } else if (fx == "ai") {
    triggerReaction(REACTION_AI, "AI inference complete", 1);
  } else if (fx == "storm") {
    triggerReaction(REACTION_STORM, "Network storm detected", 1);
  } else if (fx == "github") {
    triggerReaction(REACTION_GITHUB, "GitHub fork requested", 1);
  } else if (fx == "linkedin") {
    triggerReaction(REACTION_LINKEDIN, "LinkedIn connection requested", 1);
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleContactSubmit() {
  String name = cleanInput(server.arg("name"), 32);
  String contact = cleanInput(server.arg("contact"), 80);
  String note = cleanInput(server.arg("note"), 120);

  if (name.length() == 0 && contact.length() == 0 && note.length() == 0) {
    lastSignal = "Blank packet card ignored";
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }

  if (contactPacketCount >= MAX_CONTACTS) {
    lastSignal = "Contact storage full";
    server.send(507, "text/plain", "Contact storage is full. Ask Marshall to export/clear contacts.");
    return;
  }

  uint8_t index = contactPacketCount;
  prefs.putString(keyFor("nm", index).c_str(), name);
  prefs.putString(keyFor("ct", index).c_str(), contact);
  prefs.putString(keyFor("nt", index).c_str(), note);

  contactPacketCount++;
  triggerReaction(REACTION_CONTACT, "Contact packet received", CONTACT_PACKET_VALUE);

  addActivity(ACTIVITY_CONTACT, name.length() > 0 ? "Contact from " + name : "Contact card received");

  server.send(200, "text/html", contactSuccessPage(name));
}

void handleAdminLogin() {
  bool wrongKey = server.hasArg("error") && server.arg("error") == "1";
  // The login form posts the key directly to whichever target the caller
  // wants. Default to /contacts (the original admin destination); accept
  // ?next=/console (or any /-prefixed path) to support other admin pages.
  String nextPath = "/contacts";
  if (server.hasArg("next")) {
    String requested = server.arg("next");
    if (requested.length() > 0 && requested.startsWith("/")) {
      nextPath = requested;
    }
  }
  server.send(200, "text/html", adminLoginPage(wrongKey, nextPath));
}

void handleContactsAdmin() {
  if (!adminAllowed()) {
    // If the user supplied a key but it was wrong, bounce to the login page
    // with an error message instead of returning the bare 403.
    if (server.hasArg("key")) {
      server.sendHeader("Location", "/admin?error=1");
      server.send(303);
      return;
    }
    server.send(403, "text/plain", "Forbidden. Add ?key=YOUR_ADMIN_KEY");
    return;
  }

  server.send(200, "text/html", adminContactsPage());
}

void handleContactsCsv() {
  if (!adminAllowed()) {
    server.send(403, "text/plain", "Forbidden. Add ?key=YOUR_ADMIN_KEY");
    return;
  }

  String csv = "number,name,contact,note\n";

  for (uint8_t i = 0; i < contactPacketCount && i < MAX_CONTACTS; i++) {
    String name = prefs.getString(keyFor("nm", i).c_str(), "");
    String contact = prefs.getString(keyFor("ct", i).c_str(), "");
    String note = prefs.getString(keyFor("nt", i).c_str(), "");

    csv += String(i + 1) + "," + csvEscape(name) + "," + csvEscape(contact) + "," + csvEscape(note) + "\n";
  }

  server.sendHeader("Content-Disposition", "attachment; filename=badge_contacts.csv");
  server.send(200, "text/csv", csv);
}

void handleClearContacts() {
  if (!adminAllowed()) {
    server.send(403, "text/plain", "Forbidden. Add ?key=YOUR_ADMIN_KEY");
    return;
  }

  for (uint8_t i = 0; i < MAX_CONTACTS; i++) {
    prefs.remove(keyFor("nm", i).c_str());
    prefs.remove(keyFor("ct", i).c_str());
    prefs.remove(keyFor("nt", i).c_str());
  }

  contactPacketCount = 0;
  saveSettings();
  lastSignal = "Contacts cleared";

  server.sendHeader("Location", String("/contacts?key=") + activeAdminKey);
  server.send(303);
}

void handleAdminKeyReveal() {
  if (digitalRead(BOOT_BUTTON) == LOW) {
    server.send(200, "text/plain", activeAdminKey);
  } else {
    server.send(403, "text/plain", "Hold the BOOT button on the badge and reload this page.");
  }
}

String consolePage() {
  String html = "";
  html += "<!doctype html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Badge Console</title>";
  html += "<style>";
  html += ":root{color-scheme:dark;}";
  html += "body{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;";
  html += "background:#090d14;color:#dbe8ff;margin:0;padding:18px;min-height:100vh;}";
  html += ".card{max-width:760px;margin:auto;background:#121a27;";
  html += "border:1px solid #33435c;border-radius:18px;padding:18px;}";
  html += "h1{font-size:20px;margin:0 0 6px;color:#fff;}";
  html += "p{color:#bec8d8;margin:4px 0 14px;}";
  html += ".log{background:#070b12;border:1px solid #2a364c;border-radius:12px;";
  html += "padding:12px;height:340px;overflow-y:auto;white-space:pre-wrap;";
  html += "font-size:13px;line-height:1.45;color:#a8d8ff;}";
  html += ".log .empty{color:#5c6c83;font-style:italic;}";
  html += "form{display:flex;gap:8px;margin-top:14px;}";
  html += "input{flex:1;border:1px solid #3a4e6c;background:#172235;color:white;";
  html += "border-radius:10px;padding:11px 12px;font:inherit;font-size:14px;}";
  html += "button{border:0;background:#2ee58f;color:#06120b;font-weight:800;";
  html += "padding:11px 16px;border-radius:10px;cursor:pointer;font-size:14px;}";
  html += ".small{font-size:12px;color:#7a8aa3;margin-top:14px;}";
  html += ".small code{background:#172235;padding:1px 6px;border-radius:6px;}";
  html += "a{color:#7dffca;text-decoration:none;}";
  html += "</style></head><body>";

  html += "<div class='card'>";
  html += "<h1>Badge Console</h1>";
  html += "<p>Type a command below. Output is streamed to the log.</p>";

  html += "<div class='log'>";
  if (consoleLogCount == 0) {
    html += "<div class='empty'>No output yet. Try a command.</div>";
  } else {
    // Render oldest-first so the log scrolls naturally.
    uint8_t start = (consoleLogHead + CONSOLE_LOG_LINES - consoleLogCount) % CONSOLE_LOG_LINES;
    for (uint8_t i = 0; i < consoleLogCount; i++) {
      uint8_t idx = (start + i) % CONSOLE_LOG_LINES;
      html += escapeHtml(consoleLogBuffer[idx]) + "\n";
    }
  }
  html += "</div>";

  html += "<form action='/console' method='get'>";
  html += "<input type='hidden' name='key' value='" + activeAdminKey + "'>";
  html += "<input name='cmd' placeholder='setkey=newvalue, clearkey, factoryreset...' autocomplete='off' autofocus>";
  html += "<button type='submit'>Run</button>";
  html += "</form>";

  html += "<p class='small'>Available commands: <code>setkey=&lt;value&gt;</code>, <code>clearkey</code>, <code>factoryreset</code>. Anything else is logged as Unknown command.</p>";
  html += "<p class='small'><a href='/contacts?key=" + activeAdminKey + "'>&rarr; View Contacts</a></p>";
  html += "</div>";

  // Auto-scroll the log to the bottom on every page load so newest output is visible.
  html += "<script>(function(){var l=document.querySelector('.log');if(l)l.scrollTop=l.scrollHeight;})();</script>";
  html += "</body></html>";
  return html;
}

void handleConsole() {
  if (!adminAllowed()) {
    if (server.hasArg("key")) {
      server.sendHeader("Location", "/admin?error=1&next=/console");
      server.send(303);
      return;
    }
    server.sendHeader("Location", "/admin?next=/console");
    server.send(303);
    return;
  }

  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    cmd.trim();
    if (cmd.length() > 0) {
      consoleLog("> " + cmd);
      processSerialLine(cmd);
    }
    // Redirect after the command so a refresh does not re-run it (PRG pattern).
    // The redirect carries the active key so the page stays accessible.
    server.sendHeader("Location", "/console?key=" + activeAdminKey);
    server.send(303);
    return;
  }

  server.send(200, "text/html", consolePage());
}

void handleNext() {
  idlePattern = (idlePattern + 1) % 4;
  lastSignal = "Idle pattern changed";
  saveSettings();

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleBrightness() {
  if (server.hasArg("b")) {
    brightness = constrain(server.arg("b").toInt(), 5, 120);
    saveSettings();
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleResetCount() {
  packetCount = 0;
  peerSeenCount = 0;
  seenPeerSlots = 0;
  lastSignal = "Mesh score reset";
  saveSettings();

  server.sendHeader("Location", "/");
  server.send(303);
}

// =============================================================================
// Section 10: Input
// =============================================================================

void checkButton() {
  if (millis() - lastButtonCheck < 30) return;
  lastButtonCheck = millis();

  bool buttonState = digitalRead(BOOT_BUTTON);

  if (lastButtonState == HIGH && buttonState == LOW) {
    idlePattern = (idlePattern + 1) % 4;
    lastSignal = "BOOT button";
    saveSettings();

    activeReaction = REACTION_LINKUP;
    reactionStart = millis();
  }

  lastButtonState = buttonState;
}

// =============================================================================
// Section 11: Entry points
// =============================================================================

void setup() {
  setCpuFrequencyMhz(80);  // Underclock to reduce power and die temp; 80 MHz is the minimum for Wi-Fi.
  Serial.begin(115200);
  delay(300);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  prefs.begin("badge", false);

  // Hold-BOOT-on-power-up factory reset.
  // If the BOOT button is held LOW continuously for 5 seconds after power-on,
  // wipe the NVS namespace 'badge' and reboot. The LED strip pulses red as a
  // warning so accidental presses are obvious and easy to abort by releasing.
  if (digitalRead(BOOT_BUTTON) == LOW) {
    pixels.begin();
    pixels.setBrightness(64);
    Serial.println("BOOT held on power-up. Hold for 5 seconds to factory reset.");

    const unsigned long resetHoldMs = 5000;
    unsigned long start = millis();
    bool aborted = false;

    while (millis() - start < resetHoldMs) {
      if (digitalRead(BOOT_BUTTON) == HIGH) {
        aborted = true;
        break;
      }
      // Red pulse, intensity ramps with hold progress.
      unsigned long held = millis() - start;
      uint8_t level = (uint8_t)(20 + (held * 235UL) / resetHoldMs);
      for (int i = 0; i < NUM_LEDS; i++) {
        pixels.setPixelColor(i, pixels.Color(level, 0, 0));
      }
      pixels.show();
      delay(40);
    }

    pixels.clear();
    pixels.show();

    if (!aborted) {
      factoryResetAndReboot();
      // unreachable
    }
    Serial.println("Factory reset aborted (BOOT released).");
  }

  loadSettings();
  activeAdminKey = loadActiveAdminKey();
  activeApSsid = resolveApSsid();
  activeBleName = resolveBleName();

  // Internal temperature sensor (die temp, not ambient).
  temperature_sensor_config_t tempCfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  temperature_sensor_install(&tempCfg, &tempSensor);
  temperature_sensor_enable(tempSensor);

  pixels.begin();
  pixels.setBrightness(brightness);
  pixels.clear();
  pixels.show();

  randomSeed(esp_random());

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(activeApSsid.c_str());

  dnsServer.start(DNS_PORT, "*", apIP);

  // Advertise the badge over mDNS as <MDNS_HOSTNAME>.local for friendlier URLs
  // on devices that bind their resolver to the AP. The captive portal still
  // catches unknown DNS lookups, so this is an enhancement, not a fallback.
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", handleRoot);
  server.on("/trigger", handleTrigger);
  server.on("/contact", HTTP_POST, handleContactSubmit);
  server.on("/contacts", handleContactsAdmin);
  server.on("/contacts.csv", handleContactsCsv);
  server.on("/clearcontacts", handleClearContacts);
  server.on("/admin", handleAdminLogin);
  server.on("/next", handleNext);
  server.on("/brightness", handleBrightness);
  server.on("/resetcount", handleResetCount);
  server.on("/admin/key", handleAdminKeyReveal);
  server.on("/console", handleConsole);

  // Common captive portal detection URLs
  server.on("/generate_204", handleCaptivePortalProbe);              // Android
  server.on("/gen_204", handleCaptivePortalProbe);                   // Android / Chrome
  server.on("/hotspot-detect.html", handleCaptivePortalProbe);       // Apple
  server.on("/library/test/success.html", handleCaptivePortalProbe); // Apple
  server.on("/success.txt", handleCaptivePortalProbe);                  // Apple / CNA variants
  server.on("/success.html", handleCaptivePortalProbe);                 // Apple / CNA variants
  server.on("/blank.html", handleCaptivePortalProbe);                   // Apple / CNA variants
  server.on("/ncsi.txt", handleCaptivePortalProbe);                  // Windows
  server.on("/connecttest.txt", handleCaptivePortalProbe);           // Windows
  server.on("/redirect", redirectToPortal);                          // Windows

  server.onNotFound(handleNotFound);

  server.begin();

  // BLE comes up *after* HTTP is serving so the captive portal pops before
  // the BLE stack init delays the loop.
  startBlePresence();

  Serial.println();
  Serial.println("I NETWORK WITH AI badge started");
  Serial.print("Connect to Wi-Fi: ");
  Serial.println(activeApSsid);
    Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
  Serial.print("Or try: http://");
  Serial.print(MDNS_HOSTNAME);
  Serial.println(".local");
  Serial.print("Admin contacts: http://");
  Serial.print(WiFi.softAPIP());
  Serial.print("/contacts?key=");
  Serial.println(activeAdminKey);

  triggerReaction(REACTION_AI, "Badge booted", 0);
}

void loop() {
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
  }
  dnsServer.processNextRequest();
  server.handleClient();
  runBlePresenceScan();
  runLeaderboardSync();
  checkButton();

  if (millis() - lastFrame > 55) {
    lastFrame = millis();

    pixels.setBrightness(brightness);

    if (activeReaction != REACTION_NONE) {
      renderReaction();
    } else {
      renderIdle();
    }

    pixels.show();
    frame++;
  }
}
