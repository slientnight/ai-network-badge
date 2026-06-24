// =============================================================================
// AI Network Badge — LCD variant (Waveshare ESP32-C6-LCD-1.47)
// ST7789 172x320 display, Wi-Fi 6, BLE 5
// =============================================================================
// Section 1: Includes
// =============================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <math.h>
#include "driver/temperature_sensor.h"
#include "esp_sleep.h"
#include <Update.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>

// =============================================================================
// CONFIG — Edit values in this block to personalize the badge.
// Everything below this block is implementation and should not need editing.
// =============================================================================

// ---- Change these before taking the badge to an event ----------------------
//   BADGE_OWNER, BADGE_TITLE, LINKEDIN_URL, GITHUB_URL,
//   AP_SSID_OVERRIDE, BLE_BADGE_NAME_OVERRIDE, BLE_BADGE_PREFIX
// ----------------------------------------------------------------------------

const char* BADGE_OWNER = "Marshall";
const char* BADGE_TITLE = "Network & Systems Architect";
const char* LINKEDIN_URL = "https://www.linkedin.com/in/marshall-hollis";
const char* GITHUB_URL   = "https://github.com/slientnight";

// Leave empty ("") to auto-generate from the chip MAC as AI-BADGE-XXXX.
const char* BLE_BADGE_NAME_OVERRIDE = "";
const char* BLE_BADGE_PREFIX = "AI-BADGE";
const char* MDNS_HOSTNAME = "badge";
const char* AP_SSID_OVERRIDE = "";

// Debug flags
#define DEBUG_BLE_RSSI 0

// =============================================================================
// END CONFIG
// =============================================================================

// =============================================================================
// Section 2: Hardware and limits
// =============================================================================
#define BOOT_BUTTON 9

const char* AP_PASS = "";

const uint8_t MAX_CONTACTS = 25;
const uint8_t CONTACT_PACKET_VALUE = 3;

const unsigned long BLE_SCAN_INTERVAL_MS = 20000;
const unsigned long PEER_NEARBY_TIMEOUT_MS = 60000;
const uint32_t BLE_SCAN_SECONDS = 3;
const uint8_t MAX_SEEN_PEERS = 12;
const unsigned long REACTION_MS = 4500;

const uint8_t ACTIVITY_BUFFER_SIZE = 8;
const uint16_t SERIAL_LINE_MAX = 96;
const uint8_t CONSOLE_LOG_LINES = 24;

// BLE GATT UUIDs
#define BADGE_SERVICE_UUID    "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define BADGE_SCORE_CHAR_UUID "a1b2c3d4-e5f6-7890-abcd-ef1234567891"
#define BADGE_NAME_CHAR_UUID  "a1b2c3d4-e5f6-7890-abcd-ef1234567892"

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// LCD colors
#define BG_COLOR      0x0841  // Very dark blue-gray
#define TEXT_COLOR    TFT_WHITE
#define ACCENT_COLOR  0x07FF  // Cyan
#define LEVEL_COLOR   0x47EA  // Green-cyan
#define DIM_COLOR     0x6B4D  // Muted gray

// =============================================================================
// Section 3: Globals
// =============================================================================

struct PeerRecord {
  String name;
  String displayName;
  int8_t rssiFirst;
  int8_t rssiLast;
  unsigned long firstSeenMs;
  unsigned long lastSeenMs;
  uint32_t score;
  unsigned long scoreFetchedMs;
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

// LCD pins (Waveshare ESP32-C6-LCD-1.47)
#define LCD_CS   14
#define LCD_DC   15
#define LCD_RST  21
#define LCD_BL   22
#define LCD_MOSI  6
#define LCD_SCK   7

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, -1 /* MISO */);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0 /* rotation */, true /* IPS */, 172, 320);
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

uint32_t packetCount = 0;
uint32_t contactPacketCount = 0;
uint32_t peerSeenCount = 0;

NimBLEScan* bleScan = nullptr;
NimBLECharacteristic* scoreCharacteristic = nullptr;
NimBLECharacteristic* nameCharacteristic = nullptr;
temperature_sensor_handle_t tempSensor = NULL;
unsigned long lastBleScan = 0;

PeerRecord seenPeers[MAX_SEEN_PEERS];
uint8_t seenPeerSlots = 0;

unsigned long lastButtonCheck = 0;
bool lastButtonState = HIGH;
unsigned long lastDisplayUpdate = 0;

String lastSignal = "None yet";

String activeAdminKey = "";
String activeApSsid = "";
String activeBleName = "";

String cfgOwner = "";
String cfgTitle = "";
String cfgLinkedIn = "";
String cfgGitHub = "";

ActivityEntry activityBuffer[ACTIVITY_BUFFER_SIZE];
uint8_t activityHead = 0;
uint8_t activityCount = 0;
String serialLine = "";

String consoleLogBuffer[CONSOLE_LOG_LINES];
uint8_t consoleLogHead = 0;
uint8_t consoleLogCount = 0;

// =============================================================================
// Section 4: Helpers
// =============================================================================

String badgeLevelNameForCount(uint32_t count) {
  if (count >= 50) return "Supernode";
  if (count >= 25) return "AI Router";
  if (count >= 10) return "Mesh Builder";
  if (count >= 5)  return "Linked Node";
  if (count >= 1)  return "Listening Node";
  return "Offline Node";
}

String badgeLevelName() { return badgeLevelNameForCount(packetCount); }

uint32_t nextUnlockCount() {
  if (packetCount < 1)  return 1;
  if (packetCount < 5)  return 5;
  if (packetCount < 10) return 10;
  if (packetCount < 25) return 25;
  if (packetCount < 50) return 50;
  return 0;
}

void saveSettings() {
  prefs.putUInt("packetCount", packetCount);
  prefs.putUInt("contactCount", contactPacketCount);
  prefs.putUInt("peerSeen", peerSeenCount);
  if (scoreCharacteristic != nullptr) {
    scoreCharacteristic->setValue(packetCount);
  }
  if (nameCharacteristic != nullptr) {
    nameCharacteristic->setValue(cfgOwner.c_str());
  }
}

void loadSettings() {
  packetCount = prefs.getUInt("packetCount", 0);
  contactPacketCount = prefs.getUInt("contactCount", 0);
  peerSeenCount = prefs.getUInt("peerSeen", 0);
}

void loadConfig() {
  cfgOwner = prefs.getString("cfgOwner", BADGE_OWNER);
  cfgTitle = prefs.getString("cfgTitle", BADGE_TITLE);
  cfgLinkedIn = prefs.getString("cfgLinkedin", LINKEDIN_URL);
  cfgGitHub = prefs.getString("cfgGithub", GITHUB_URL);
}

String macDerivedAdminKey() {
  uint8_t mac[8] = {0};
  esp_efuse_mac_get_default(mac);
  char key[5];
  snprintf(key, sizeof(key), "%02x%02x", mac[4], mac[5]);
  return String(key);
}

String macDerivedSuffix() {
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
  if (stored.length() > 0) return stored;
  return macDerivedAdminKey();
}

String cleanInput(String value, uint16_t maxLen) {
  value.trim();
  value.replace("\r", " ");
  value.replace("\n", " ");
  while (value.indexOf("  ") >= 0) value.replace("  ", " ");
  if (value.length() > maxLen) value = value.substring(0, maxLen);
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

String rssiLabel(int8_t rssi) {
  if (rssi >= -40) return "Adjacent";
  if (rssi >= -60) return "Near";
  if (rssi >= -80) return "Close";
  if (rssi >= -90) return "Far";
  return "Distant";
}

float readChipTempC() {
  float t = 0.0;
  if (tempSensor != NULL) temperature_sensor_get_celsius(tempSensor, &t);
  return t;
}

void addActivity(ActivityCategory category, String label) {
  activityBuffer[activityHead].label = label;
  activityBuffer[activityHead].timestamp = millis();
  activityBuffer[activityHead].category = category;
  activityHead = (activityHead + 1) % ACTIVITY_BUFFER_SIZE;
  if (activityCount < ACTIVITY_BUFFER_SIZE) activityCount++;
}

void consoleLog(String line) {
  Serial.println(line);
  consoleLogBuffer[consoleLogHead] = line;
  consoleLogHead = (consoleLogHead + 1) % CONSOLE_LOG_LINES;
  if (consoleLogCount < CONSOLE_LOG_LINES) consoleLogCount++;
}

bool adminAllowed() {
  return server.hasArg("key") && server.arg("key") == activeAdminKey;
}

bool peersNearby() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < seenPeerSlots; i++) {
    if (now - seenPeers[i].lastSeenMs < PEER_NEARBY_TIMEOUT_MS) return true;
  }
  return false;
}

// =============================================================================
// Section 5: LCD Display
// =============================================================================

void lcdDrawBadge() {
  gfx->fillScreen(BG_COLOR);

  // Header: I NETWORK WITH AI
  gfx->setTextColor(ACCENT_COLOR);
  gfx->setTextSize(2);
  int16_t tw;
  tw = 9 * 2 * 6; // approximate "I NETWORK" width
  gfx->setCursor((172 - tw) / 2 + 10, 10);
  gfx->print("I NETWORK");
  tw = 7 * 2 * 6;
  gfx->setCursor((172 - tw) / 2 + 10, 34);
  gfx->print("WITH AI");

  // Owner name and title
  gfx->setTextSize(1);
  gfx->setTextColor(TEXT_COLOR);
  gfx->setCursor(10, 68);
  gfx->print(cfgOwner);
  gfx->setTextColor(DIM_COLOR);
  gfx->setCursor(10, 82);
  gfx->print(cfgTitle);

  // Divider
  gfx->drawFastHLine(10, 98, 152, DIM_COLOR);

  // Level
  gfx->setTextColor(LEVEL_COLOR);
  gfx->setTextSize(2);
  gfx->setCursor(10, 106);
  gfx->print(badgeLevelName());

  // Stats
  gfx->setTextSize(1);
  gfx->setTextColor(TEXT_COLOR);
  int y = 132;
  gfx->setCursor(10, y); gfx->print("Packets: "); gfx->print(packetCount);
  y += 14;
  gfx->setCursor(10, y); gfx->print("Cards: "); gfx->print(contactPacketCount);
  y += 14;
  gfx->setCursor(10, y); gfx->print("Peers: "); gfx->print(peerSeenCount);
  y += 14;

  uint32_t next = nextUnlockCount();
  gfx->setTextColor(DIM_COLOR);
  gfx->setCursor(10, y);
  if (next > 0) {
    gfx->print("Next: "); gfx->print(next - packetCount); gfx->print(" more");
  } else {
    gfx->setTextColor(LEVEL_COLOR);
    gfx->print("All unlocks active");
  }
  y += 20;

  // Divider
  gfx->drawFastHLine(10, y, 152, DIM_COLOR);
  y += 8;

  // Nearby peers
  gfx->setTextColor(ACCENT_COLOR);
  gfx->setCursor(10, y); gfx->print("Nearby Badges");
  y += 14;

  uint8_t shown = 0;
  for (uint8_t i = 0; i < seenPeerSlots && shown < 3; i++) {
    if (millis() - seenPeers[i].lastSeenMs < PEER_NEARBY_TIMEOUT_MS) {
      String label = seenPeers[i].displayName.length() > 0
                       ? seenPeers[i].displayName
                       : seenPeers[i].name;
      gfx->setTextColor(TEXT_COLOR);
      gfx->setCursor(10, y); gfx->print(label.substring(0, 18));
      y += 12;
      shown++;
    }
  }
  if (shown == 0) {
    gfx->setTextColor(DIM_COLOR);
    gfx->setCursor(10, y); gfx->print("No peers nearby");
    y += 12;
  }

  // Footer
  gfx->setTextColor(DIM_COLOR);
  gfx->setCursor(10, 296);
  gfx->print("WiFi: "); gfx->print(activeApSsid);
  float tempC = readChipTempC();
  gfx->setCursor(10, 308);
  gfx->print(String(tempC * 9.0 / 5.0 + 32.0, 0) + "F / " + String(tempC, 0) + "C");
}

void updateDisplay() {
  // Only redraw every 5 seconds to avoid flicker.
  if (millis() - lastDisplayUpdate < 5000) return;
  lastDisplayUpdate = millis();
  lcdDrawBadge();
}

// =============================================================================
// Section 6: BLE
// =============================================================================

void rememberPeer(String peerName, int8_t rssi) {
  if (peerName.length() == 0) return;
  if (peerName == activeBleName) return;
  if (!peerName.startsWith(BLE_BADGE_PREFIX)) return;

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

  uint8_t idx;
  if (seenPeerSlots < MAX_SEEN_PEERS) {
    idx = seenPeerSlots++;
  } else {
    idx = 0;
  }
  seenPeers[idx].name = peerName;
  seenPeers[idx].displayName = "";
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
  addActivity(ACTIVITY_PEER, "Peer: " + peerName);
}

class BadgeAdvertisedDeviceCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    if (!advertisedDevice->haveName()) return;
    String peerName = String(advertisedDevice->getName().c_str());
    rememberPeer(peerName, (int8_t)advertisedDevice->getRSSI());
  }
};

void startBlePresence() {
  NimBLEDevice::init(activeBleName.c_str());

  NimBLEServer* pServer = NimBLEDevice::createServer();
  NimBLEService* pService = pServer->createService(BADGE_SERVICE_UUID);

  scoreCharacteristic = pService->createCharacteristic(
    BADGE_SCORE_CHAR_UUID, NIMBLE_PROPERTY::READ);
  scoreCharacteristic->setValue(packetCount);

  nameCharacteristic = pService->createCharacteristic(
    BADGE_NAME_CHAR_UUID, NIMBLE_PROPERTY::READ);
  nameCharacteristic->setValue(cfgOwner.c_str());

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
  NimBLEDevice::startAdvertising();
}

// =============================================================================
// Section 7: Web server (captive portal still works — LCD is bonus display)
// =============================================================================

void handleRoot() {
  // Simplified home page — the LCD shows the main info now.
  String html = "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>I Network With AI</title>";
  html += "<style>:root{color-scheme:dark;}body{font-family:system-ui;background:#090d14;color:white;margin:0;padding:22px;}";
  html += ".card{max-width:520px;margin:auto;background:#121a27;border:1px solid #33435c;border-radius:24px;padding:24px;}";
  html += "h1{font-size:32px;margin:0 0 8px;}p{color:#bec8d8;line-height:1.45;}";
  html += ".level{font-size:22px;font-weight:900;color:#7dffca;margin:8px 0;}";
  html += ".pill{display:inline-block;background:#26344a;border:1px solid #3a4e6c;border-radius:999px;padding:6px 12px;margin:4px 4px 4px 0;color:#dbe8ff;font-size:14px;}";
  html += "a,button{display:block;width:100%;box-sizing:border-box;margin:10px 0;padding:14px;border-radius:14px;text-align:center;text-decoration:none;font-weight:800;font-size:15px;}";
  html += "a{background:#2f80ed;color:white;}button{border:0;background:#2ee58f;color:#06120b;cursor:pointer;}";
  html += ".secondary{background:#26344a;color:#e8f0ff;}.small{font-size:13px;color:#91a0b7;}";
  html += "</style></head><body><div class='card'>";
  html += "<h1>I NETWORK WITH AI</h1>";
  html += "<p>Hi, I'm <b>" + escapeHtml(cfgOwner) + "</b>. " + escapeHtml(cfgTitle) + "</p>";
  html += "<div class='level'>" + badgeLevelName() + "</div>";
  html += "<span class='pill'>Packets: " + String(packetCount) + "</span>";
  html += "<span class='pill'>Cards: " + String(contactPacketCount) + "</span>";
  html += "<span class='pill'>Peers: " + String(peerSeenCount) + "</span>";

  if (cfgLinkedIn.length() > 0)
    html += "<a href='" + escapeHtml(cfgLinkedIn) + "' target='_blank'>Connect on LinkedIn</a>";
  if (cfgGitHub.length() > 0)
    html += "<a href='" + escapeHtml(cfgGitHub) + "' target='_blank'>View GitHub</a>";

  html += "<h2 style='font-size:16px;margin-top:20px;color:#d9e6ff;'>Send a Packet</h2>";
  html += "<form action='/trigger' method='get'><button name='fx' value='packet'>Send Packet (+1)</button></form>";

  html += "<div style='margin-top:16px;'>";
  html += "<a class='secondary' href='/contact'>Leave a Contact Card (+3)</a>";
  html += "<a class='secondary' href='/admin'>Owner Panel</a>";
  html += "</div>";

  html += "<p class='small'>WiFi: " + activeApSsid + " | BLE: " + activeBleName + "</p>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleTrigger() {
  packetCount++;
  lastSignal = "Packet from web";
  saveSettings();
  addActivity(ACTIVITY_REACTION, "Packet");
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleContactForm() {
  String html = "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Leave a Card</title>";
  html += "<style>:root{color-scheme:dark;}body{font-family:system-ui;background:#090d14;color:white;margin:0;padding:22px;}";
  html += ".card{max-width:480px;margin:auto;background:#121a27;border:1px solid #33435c;border-radius:24px;padding:24px;}";
  html += "h1{font-size:24px;margin:0 0 8px;}p{color:#bec8d8;}";
  html += "label{display:block;font-weight:800;margin:12px 0 6px;color:#d9e6ff;}";
  html += "input,textarea{width:100%;box-sizing:border-box;border:1px solid #3a4e6c;background:#172235;color:white;border-radius:12px;padding:12px;font:inherit;}";
  html += "textarea{min-height:70px;resize:vertical;}";
  html += "button{display:block;width:100%;border:0;background:#2ee58f;color:#06120b;font-weight:900;font-size:16px;padding:14px;border-radius:14px;cursor:pointer;margin-top:16px;}";
  html += "</style></head><body><div class='card'>";
  html += "<h1>Leave a Packet Card</h1>";
  html += "<p>Stored locally on this badge only.</p>";
  html += "<form action='/contact' method='post'>";
  html += "<label>Name</label><input name='name' maxlength='32' placeholder='Your name'>";
  html += "<label>Contact</label><input name='contact' maxlength='80' placeholder='Email, LinkedIn, or GitHub'>";
  html += "<label>Note</label><textarea name='note' maxlength='120' placeholder='Where did we meet?'></textarea>";
  html += "<button type='submit'>Send Contact Packet (+3)</button>";
  html += "</form></div></body></html>";
  server.send(200, "text/html", html);
}

void handleContactSubmit() {
  String name = cleanInput(server.arg("name"), 32);
  String contact = cleanInput(server.arg("contact"), 80);
  String note = cleanInput(server.arg("note"), 120);

  uint8_t count = prefs.getUChar("contactCnt", 0);
  if (count < MAX_CONTACTS) {
    char key[8];
    snprintf(key, sizeof(key), "cn%02u", count);
    prefs.putString(key, name);
    snprintf(key, sizeof(key), "cc%02u", count);
    prefs.putString(key, contact);
    snprintf(key, sizeof(key), "co%02u", count);
    prefs.putString(key, note);
    prefs.putUChar("contactCnt", count + 1);
  }

  contactPacketCount++;
  packetCount += CONTACT_PACKET_VALUE;
  saveSettings();
  addActivity(ACTIVITY_CONTACT, "Card: " + name);

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleCaptivePortalProbe() {
  server.sendHeader("Location", "http://192.168.4.1/");
  server.send(302);
}

void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1/");
  server.send(302);
}

// =============================================================================
// Section 8: Setup and Loop
// =============================================================================

void setup() {
  setCpuFrequencyMhz(160);  // C6 is single-core; keep at 160 for LCD rendering.
  Serial.begin(115200);
  delay(300);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  // LCD init
  gfx->begin();
  gfx->fillScreen(BG_COLOR);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);  // Backlight on

  gfx->setTextColor(ACCENT_COLOR);
  gfx->setTextSize(1);
  gfx->setCursor(30, 150);
  gfx->print("Booting...");

  prefs.begin("badge", false);
  loadSettings();
  loadConfig();
  activeAdminKey = loadActiveAdminKey();
  activeApSsid = resolveApSsid();
  activeBleName = resolveBleName();

  // Temperature sensor
  temperature_sensor_config_t tempCfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  temperature_sensor_install(&tempCfg, &tempSensor);
  temperature_sensor_enable(tempSensor);

  // Wi-Fi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(activeApSsid.c_str());

  dnsServer.start(DNS_PORT, "*", apIP);

  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }

  // Routes
  server.on("/", handleRoot);
  server.on("/trigger", handleTrigger);
  server.on("/contact", HTTP_GET, handleContactForm);
  server.on("/contact", HTTP_POST, handleContactSubmit);

  // Captive portal probes
  server.on("/generate_204", handleCaptivePortalProbe);
  server.on("/gen_204", handleCaptivePortalProbe);
  server.on("/hotspot-detect.html", handleCaptivePortalProbe);
  server.on("/library/test/success.html", handleCaptivePortalProbe);
  server.on("/success.txt", handleCaptivePortalProbe);
  server.on("/ncsi.txt", handleCaptivePortalProbe);
  server.on("/connecttest.txt", handleCaptivePortalProbe);
  server.on("/redirect", handleCaptivePortalProbe);

  server.onNotFound(handleNotFound);
  server.begin();

  // BLE
  startBlePresence();

  Serial.println("AI Network Badge (LCD) started");
  Serial.print("WiFi: ");
  Serial.println(activeApSsid);
  Serial.print("Admin key: ");
  Serial.println(activeAdminKey);

  // Draw initial badge screen
  lcdDrawBadge();
}

void loop() {
  // Serial commands
  while (Serial.available() > 0) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (serialLine.length() > 0) {
        serialLine.trim();
        if (serialLine.startsWith("setkey=")) {
          String v = serialLine.substring(7); v.trim();
          if (v.length() > 0) { prefs.putString("adminKey", v); activeAdminKey = v; consoleLog("Key set: " + v); }
        } else if (serialLine == "clearkey") {
          prefs.remove("adminKey"); activeAdminKey = macDerivedAdminKey(); consoleLog("Key cleared: " + activeAdminKey);
        } else if (serialLine == "factoryreset") {
          prefs.clear(); prefs.end(); delay(200); ESP.restart();
        }
        serialLine = "";
      }
    } else if (serialLine.length() < SERIAL_LINE_MAX) {
      serialLine += ch;
    }
  }

  dnsServer.processNextRequest();
  server.handleClient();
  runBlePresenceScan();

  // Update LCD periodically
  updateDisplay();
}
