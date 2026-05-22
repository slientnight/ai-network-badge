// =============================================================================
// Section 1: Includes
// =============================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <math.h>

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

// =============================================================================
// Section 3: Hardware and limits
// =============================================================================
#define LED_PIN 4
#define BOOT_BUTTON 9
#define NUM_LEDS 8   // Badge LED count.

const char* AP_SSID = "AI-BADGE";
const char* AP_PASS = "";  // Open Wi-Fi network, no password.

const uint8_t MAX_CONTACTS = 25;
const uint8_t CONTACT_PACKET_VALUE = 3;

const unsigned long BLE_SCAN_INTERVAL_MS = 20000;
const uint32_t BLE_SCAN_SECONDS = 3;
const uint8_t MAX_SEEN_PEERS = 12;
const unsigned long REACTION_MS = 4500;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);
// =============================================================================
// Section 4: Globals and runtime state
// =============================================================================

Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

uint8_t brightness = 32;
uint8_t idlePattern = 0;
uint32_t packetCount = 0;
uint32_t contactPacketCount = 0;
uint32_t peerSeenCount = 0;

BLEScan* bleScan = nullptr;
unsigned long lastBleScan = 0;

String seenPeers[MAX_SEEN_PEERS];
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
}

void loadSettings() {
  brightness = prefs.getUChar("brightness", 32);
  idlePattern = prefs.getUChar("idlePattern", 0);
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

bool adminAllowed() {
  return server.hasArg("key") && server.arg("key") == ADMIN_KEY;
}

bool peerAlreadySeen(String peerName) {
  for (uint8_t i = 0; i < seenPeerSlots; i++) {
    if (seenPeers[i] == peerName) {
      return true;
    }
  }
  return false;
}

void rememberPeer(String peerName) {
  if (peerName.length() == 0) return;
  if (peerName == String(BLE_BADGE_NAME)) return;
  if (!peerName.startsWith(BLE_BADGE_PREFIX)) return;
  if (peerAlreadySeen(peerName)) return;

  if (seenPeerSlots < MAX_SEEN_PEERS) {
    seenPeers[seenPeerSlots++] = peerName;
  } else {
    // Simple ring-ish behavior: overwrite slot 0 if the demo sees many badges.
    seenPeers[0] = peerName;
  }

  peerSeenCount++;
  lastSignal = "Peer found: " + peerName;
  saveSettings();

  activeReaction = REACTION_LINKUP;
  reactionStart = millis();
}

class BadgeAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!advertisedDevice.haveName()) {
      return;
    }

    String peerName = String(advertisedDevice.getName().c_str());
    rememberPeer(peerName);
  }
};

void startBlePresence() {
  BLEDevice::init(BLE_BADGE_NAME);

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  bleScan = BLEDevice::getScan();
  bleScan->setAdvertisedDeviceCallbacks(new BadgeAdvertisedDeviceCallbacks(), true);
  bleScan->setActiveScan(true);
  bleScan->setInterval(160);
  bleScan->setWindow(80);
}

void runBlePresenceScan() {
  if (bleScan == nullptr) return;
  if (millis() - lastBleScan < BLE_SCAN_INTERVAL_MS) return;

  lastBleScan = millis();
  bleScan->start(BLE_SCAN_SECONDS, false);
  bleScan->clearResults();
  BLEDevice::startAdvertising();
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

  if (packetValue > 0) {
    uint32_t before = packetCount;
    packetCount += packetValue;

    if (crossedUnlock(before, packetCount)) {
      activeReaction = REACTION_STORM;
      reactionStart = millis();
      lastSignal = "LEVEL UP: " + badgeLevelName();
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

  html += "<p class='small'>BLE presence: <b>" + String(BLE_BADGE_NAME) + "</b>. Nearby AI badges count as peers.</p>";
  html += "<p class='small'>Admin: <b>/contacts?key=YOUR_KEY</b> and <b>/contacts.csv?key=YOUR_KEY</b></p>";
  html += "<p class='small'>Wi-Fi: <b>" + String(AP_SSID) + "</b><br>";
  html += "Open network, no password required.<br>";
  html += "This page should open automatically after joining Wi-Fi.<br>";
  html += "If not, open: <b>http://192.168.4.1</b></p>";

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
  html += "<a href='/contacts.csv?key=" + String(ADMIN_KEY) + "'>Download CSV</a>";
  html += "<a class='danger' href='/clearcontacts?key=" + String(ADMIN_KEY) + "'>Clear contacts</a>";
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
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='Cache-Control' content='no-store, no-cache, must-revalidate, max-age=0'>";
  html += "<meta http-equiv='Pragma' content='no-cache'>";
  html += "<title>I Network With AI</title>";
  html += "<style>body{font-family:system-ui;background:#090d14;color:white;margin:0;padding:24px;}";
  html += ".card{max-width:520px;margin:auto;background:#121a27;border:1px solid #33435c;border-radius:24px;padding:24px;}";
  html += "h1{font-size:36px;line-height:1.05;margin:0 0 12px}.level{color:#7dffca;font-size:22px;font-weight:900}";
  html += "a{display:block;background:#2ee58f;color:#06120b;text-align:center;text-decoration:none;font-weight:900;padding:16px;border-radius:14px;margin-top:18px}";
  html += "p{color:#bec8d8;line-height:1.45}</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>I NETWORK<br>WITH AI</h1>";
  html += "<div class='level'>BUILD THE MESH</div>";
  html += "<p>You are connected to Marshall's interactive badge. Tap below to open the badge page and send a packet.</p>";
  html += "<a href='http://192.168.4.1/'>Open Badge Page</a>";
  html += "</div></body></html>";

  return html;
}

// =============================================================================
// Section 9: HTTP handlers
// =============================================================================

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleCaptivePortalProbe() {
  // iOS/Android/Windows probe URLs should NOT receive their expected success text.
  // Returning this page makes the device more likely to treat the network as captive.
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
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

  server.send(200, "text/html", contactSuccessPage(name));
}

void handleContactsAdmin() {
  if (!adminAllowed()) {
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

  server.sendHeader("Location", String("/contacts?key=") + ADMIN_KEY);
  server.send(303);
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
  Serial.begin(115200);
  delay(300);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  prefs.begin("badge", false);
  loadSettings();

  pixels.begin();
  pixels.setBrightness(brightness);
  pixels.clear();
  pixels.show();

  randomSeed(esp_random());

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(AP_SSID);

  dnsServer.start(DNS_PORT, "*", apIP);
  startBlePresence();

  server.on("/", handleRoot);
  server.on("/trigger", handleTrigger);
  server.on("/contact", HTTP_POST, handleContactSubmit);
  server.on("/contacts", handleContactsAdmin);
  server.on("/contacts.csv", handleContactsCsv);
  server.on("/clearcontacts", handleClearContacts);
  server.on("/next", handleNext);
  server.on("/brightness", handleBrightness);
  server.on("/resetcount", handleResetCount);

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

  server.onNotFound(handleCaptivePortalProbe);

  server.begin();

  Serial.println();
  Serial.println("I NETWORK WITH AI badge started");
  Serial.print("Connect to Wi-Fi: ");
  Serial.println(AP_SSID);
    Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
  Serial.print("Admin contacts: http://");
  Serial.print(WiFi.softAPIP());
  Serial.print("/contacts?key=");
  Serial.println(ADMIN_KEY);

  triggerReaction(REACTION_AI, "Badge booted", 0);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  runBlePresenceScan();
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
