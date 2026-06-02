// ============================================================
//  SMART DOORBELL — INDOOR DISPLAY UNIT (Version 2.1)
//  Compatible boards:
//    • PANLEE BC02  (ZX3D95CE01S-TR-V12)   — no USB, uses UART programmer
//    • Wireless-Tag WT32S3-86S              — comes with programmer
//
//  Both boards are IDENTICAL hardware:
//    MCU    : ESP32-S3-WROVER-N16R8  (16MB Flash, 8MB OPI PSRAM)
//    Display: 3.95" IPS, 480×480, RGB565 parallel, GC9503V driver
//    Touch  : FT5x06, I2C
//    Extra  : SHT20 temp/humidity, RS485 (both unused here)
//
//  Libraries required (install via Arduino Library Manager):
//    1. PanelLan       by smartpanle   — handles BOARD_BC02 pin config
//    2. LovyanGFX      by lovyan03     — RGB driver + JPEG decoding
//    3. ArduinoJson    by bblanchon    — JSON polling response
//
//  Arduino IDE board settings:
//    Board           : ESP32S3 Dev Module
//    Flash Size      : 16MB (128Mb)
//    Flash Mode      : QIO 80MHz
//    PSRAM           : OPI PSRAM          ← CRITICAL
//    Partition Scheme: Huge APP (3MB...)
//    USB CDC On Boot : Disabled           ← no USB on these boards
//    Upload Speed    : 921600
//
//  Screen layout (480×480 square):
//   ┌──────────────────────────────────────┐ y=0
//   │                                      │
//   │     Visitor JPEG image (480×360)     │
//   │       (LovyanGFX scales to fit)      │
//   │                                      │
//   │     [ ADMIT ]          [ DENY ]      │ <-- V2.1: Touch buttons if PENDING
//   ├──────────────────────────────────────┤ y=360
//   │  ██  ACTION REQUIRED!  [PENDING]     │ <-- V2.1: Orange background status
//   │      2025-01-15  14:32:10            │
//   └──────────────────────────────────────┘ y=480
// ============================================================

// ════════════════════════════════════════════════════════════
//  STEP 1 — WiFi credentials
// ════════════════════════════════════════════════════════════
const char* WIFI_SSID     = "WIFINAME";
const char* WIFI_PASSWORD = "PASSWORD";

// ════════════════════════════════════════════════════════════
//  STEP 2 — Server IP  (run `ipconfig` on your PC to find it)
// ════════════════════════════════════════════════════════════
const char* SERVER_BASE = "http://[IP_ADDRESS]:5050";

// ────────────────────────────────────────────────────────────
#include "PanelLan.h"        // Wraps LovyanGFX; configures RGB bus for BC02
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// BOARD_BC02 covers both the BC02 (ZX3D95CE01S-TR) and the
// WT32S3-86S — same schematic, same GPIO assignments.
PanelLan tft(BOARD_BC02);

// ── Display geometry ─────────────────────────────────────────
#define SCREEN_W      480
#define SCREEN_H      480
#define STATUS_BAR_H  120              // Height of the bottom status bar
#define IMAGE_AREA_H  (SCREEN_H - STATUS_BAR_H)  // 360 px for image

// ── Poll interval ─────────────────────────────────────────────
#define POLL_INTERVAL_MS  2000

// ── JPEG receive buffer (in PSRAM — 8 MB available) ──────────
// 200 KB is generous; VGA quality-10 JPEG is typically 8–30 KB.
#define JPEG_BUF_SIZE  204800
static uint8_t* jpegBuf   = nullptr;

// ── State ─────────────────────────────────────────────────────
static int  lastEventId  = -1;
bool isPending = false; // Tracks if buttons should be drawn

// ─────────────────────────────────────────────────────────────
//  Colour palette (LovyanGFX colour565 values)
// ─────────────────────────────────────────────────────────────
#define CLR_BG_SUCCESS  tft.color565(15, 110, 40)    // Dark green
#define CLR_BG_DENIED   tft.color565(160, 20, 20)    // Dark red
#define CLR_BG_FAILED   tft.color565(20, 50, 130)    // Dark blue
#define CLR_BG_IDLE     tft.color565(18, 18, 28)     // Near-black
#define CLR_TEXT_MAIN   TFT_WHITE
#define CLR_TEXT_DIM    tft.color565(180, 180, 180)
#define CLR_ACCENT      tft.color565(80, 160, 255)   // Blue accent
#define CLR_BG_PENDING  tft.color565(220, 120, 0) // Orange

// ─────────────────────────────────────────────────────────────
//  Helper: determine status bar background colour
// ─────────────────────────────────────────────────────────────
uint32_t statusBgColor(const String& status) {
  if (status == "SUCCESS" || status == "MANUAL_ADMIT") return CLR_BG_SUCCESS;
  if (status == "DENIED"  || status == "MANUAL_DENY")  return CLR_BG_DENIED;
  if (status == "PENDING") return CLR_BG_PENDING;
  return CLR_BG_FAILED;
}

// ─────────────────────────────────────────────────────────────
//  Idle screen — shown between events
// ─────────────────────────────────────────────────────────────
void drawIdleScreen() {
  tft.fillScreen(CLR_BG_IDLE);

  // Doorbell icon — simple circle + dot
  tft.drawCircle(SCREEN_W / 2, SCREEN_H / 2 - 50, 48, CLR_ACCENT);
  tft.drawCircle(SCREEN_W / 2, SCREEN_H / 2 - 50, 44, CLR_ACCENT);
  tft.fillCircle(SCREEN_W / 2, SCREEN_H / 2 - 50, 10, CLR_ACCENT);

  // Title
  tft.setTextColor(CLR_TEXT_MAIN, CLR_BG_IDLE);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.drawString("Smart Doorbell", SCREEN_W / 2, SCREEN_H / 2 + 30);

  // Subtitle
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG_IDLE);
  tft.drawString("Awaiting visitor...", SCREEN_W / 2, SCREEN_H / 2 + 70);

  // WiFi OK indicator at bottom right
  tft.setFont(&fonts::Font2);
  tft.setTextColor(CLR_BG_SUCCESS, CLR_BG_IDLE);
  tft.setTextDatum(textdatum_t::bottom_right);
  tft.drawString("WiFi OK", SCREEN_W - 12, SCREEN_H - 8);

  tft.setTextDatum(textdatum_t::top_left); // Reset datum
}

// ─────────────────────────────────────────────────────────────
//  Status bar — drawn over bottom 120 px AFTER image is shown
// ─────────────────────────────────────────────────────────────
void drawStatusBar(const String& name, const String& status,
                   const String& timestamp, const String& message) {
  const int BAR_Y = SCREEN_H - STATUS_BAR_H;
  uint32_t bgCol  = statusBgColor(status);

  // ── Background fill ──────────────────────────────────────────
  tft.fillRect(0, BAR_Y, SCREEN_W, STATUS_BAR_H, bgCol);

  // ── Divider line ─────────────────────────────────────────────
  tft.drawFastHLine(0, BAR_Y, SCREEN_W, TFT_WHITE);

  // ── Status icon (left side) ───────────────────────────────────
  const char* icon = (status == "SUCCESS") ? "OK" :
                     (status == "DENIED")  ? "!!" : "??";
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(TFT_WHITE, bgCol);
  tft.setTextDatum(textdatum_t::middle_left);
  tft.drawString(icon, 16, BAR_Y + 38);

  // ── Main message (e.g. "Welcome, Alice!" or "ACCESS DENIED") ──
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(TFT_WHITE, bgCol);
  tft.setTextDatum(textdatum_t::middle_left);
  tft.drawString(message.length() > 20 ? message.substring(0, 20) : message,
                 70, BAR_Y + 38);

  // ── Timestamp (lower line) ────────────────────────────────────
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(CLR_TEXT_DIM, bgCol);
  tft.setTextDatum(textdatum_t::middle_left);
  tft.drawString(timestamp, 16, BAR_Y + 90);

  // Reset datum
  tft.setTextDatum(textdatum_t::top_left);
}

// ─────────────────────────────────────────────────────────────
//  Text-only fallback when image fetch fails
// ─────────────────────────────────────────────────────────────
void drawTextFallback(const String& name, const String& status) {
  tft.fillRect(0, 0, SCREEN_W, IMAGE_AREA_H, CLR_BG_IDLE);

  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(CLR_TEXT_MAIN, CLR_BG_IDLE);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString("Visitor at door", SCREEN_W / 2, IMAGE_AREA_H / 2 - 30);

  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG_IDLE);
  tft.drawString(name, SCREEN_W / 2, IMAGE_AREA_H / 2 + 20);
  tft.drawString("(Image unavailable)", SCREEN_W / 2, IMAGE_AREA_H / 2 + 55);

  tft.setTextDatum(textdatum_t::top_left);
}

// ─────────────────────────────────────────────────────────────
//  Fetch /latest_image, decode JPEG, render into image area.
//  LovyanGFX handles the JPEG decode internally — no TJpgDec needed.
//  Returns true on success.
// ─────────────────────────────────────────────────────────────
bool fetchAndRenderImage() {
  if (!jpegBuf) return false;

  HTTPClient http;
  http.begin(String(SERVER_BASE) + "/latest_image");
  http.setTimeout(8000);
  http.addHeader("Cache-Control", "no-cache");

  int httpCode = http.GET();
  if (httpCode != 200) {
    http.end();
    return false;
  }

  // Read stream into PSRAM buffer
  int contentLen = http.getSize();
  WiFiClient* stream = http.getStreamPtr();

  size_t toRead    = (contentLen > 0 && (size_t)contentLen < JPEG_BUF_SIZE)
                      ? (size_t)contentLen
                      : JPEG_BUF_SIZE;
  size_t bytesRead = 0;
  unsigned long tStart = millis();

  while (bytesRead < toRead && (millis() - tStart) < 6000) {
    int avail = stream->available();
    if (avail > 0) {
      size_t chunk = min((size_t)avail, toRead - bytesRead);
      bytesRead += stream->readBytes((char*)(jpegBuf + bytesRead), chunk);
    } else {
      yield();
    }
  }
  http.end();

  if (bytesRead == 0) return false;

  // Clear image area (black background behind letterboxed image)
  tft.fillRect(0, 0, SCREEN_W, IMAGE_AREA_H, TFT_BLACK);

  // ── Render JPEG using LovyanGFX's built-in decoder ─────────
  // drawJpg(data, len, x, y, maxW, maxH)
  // LovyanGFX scales to fit within (maxW × maxH), maintaining
  // aspect ratio. Image is drawn at (x, y).
  // For a 640×480 JPEG into a 480×360 area:
  //   scale = min(480/640, 360/480) = min(0.75, 0.75) = 0.75
  //   output = 480×360 — fills the area perfectly!
  tft.drawJpg(jpegBuf, (uint32_t)bytesRead,
              0, 0,           // Draw origin
              SCREEN_W,       // Max width  = 480
              IMAGE_AREA_H);  // Max height = 360

  return true;
}


// Draws two buttons near the bottom of the image area
void drawActionButtons() {
  tft.fillRoundRect(20, 280, 200, 60, 8, CLR_BG_SUCCESS);
  tft.fillRoundRect(260, 280, 200, 60, 8, CLR_BG_DENIED);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.drawString("ADMIT", 120, 310);
  tft.drawString("DENY", 360, 310);
  tft.setTextDatum(textdatum_t::top_left); // Reset
}

// ─────────────────────────────────────────────────────────────
//  Poll /latest_event; on new event_id, fetch image + redraw
// ─────────────────────────────────────────────────────────────
void checkForNewEvent() {
  HTTPClient http;
  http.begin(String(SERVER_BASE) + "/latest_event");
  http.setTimeout(5000);

  int httpCode = http.GET();
  if (httpCode != 200) {
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload)) return;  // Parse error

  int    eventId   = doc["event_id"]   | 0;
  String name      = doc["name"]       | "Unknown";
  String status    = doc["status"]     | "FAILED";
  String message   = doc["message"]    | "";
  String timestamp = doc["timestamp"]  | "";

  // No new event since last check
  if (eventId == lastEventId) return;
  lastEventId = eventId;

  // Server just booted (event_id = 0) — show idle
  if (eventId == 0 || status == "IDLE") {
    drawIdleScreen();
    return;
  }

  // ── New visitor event ─────────────────────────────────────────

  isPending = (status == "PENDING");

  bool imageOk = fetchAndRenderImage();
  if (!imageOk) {
    drawTextFallback(name, status);
  }
  
  if (isPending) drawActionButtons(); // Draw over the image
  drawStatusBar(name, status, timestamp, message);
}

// ─────────────────────────────────────────────────────────────
//  WiFi boot — blocking with animated progress on screen
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
  tft.fillScreen(CLR_BG_IDLE);
  tft.setTextColor(CLR_TEXT_MAIN, CLR_BG_IDLE);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.drawString("Smart Doorbell", SCREEN_W / 2, SCREEN_H / 2 - 60);

  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG_IDLE);
  tft.drawString("Connecting to WiFi...", SCREEN_W / 2, SCREEN_H / 2 - 20);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Animated dots while waiting
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    String line = "Please wait";
    for (int i = 0; i <= (dots % 5); i++) line += ".";
    tft.setTextColor(CLR_ACCENT, CLR_BG_IDLE);
    tft.drawString(line, SCREEN_W / 2, SCREEN_H / 2 + 20);
    dots++;
    delay(500);
  }

  // Connected confirmation
  tft.fillRect(0, SCREEN_H / 2 - 30, SCREEN_W, 90, CLR_BG_IDLE);
  tft.setTextColor(CLR_BG_SUCCESS, CLR_BG_IDLE);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.drawString("Connected!", SCREEN_W / 2, SCREEN_H / 2 - 10);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG_IDLE);
  tft.drawString(WiFi.localIP().toString(), SCREEN_W / 2, SCREEN_H / 2 + 30);

  tft.setTextDatum(textdatum_t::top_left); // Reset
  delay(1500);
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Doorbell Indoor Display (BC02 / WT32S3-86S) ===");

  // ── PanelLan init — configures RGB bus, GC9503V driver, backlight ─
  tft.begin();
  tft.setRotation(0);          // Portrait 0° — 480×480 is already square, rotation doesn't matter
  tft.setBrightness(200);      // 0–255; 200 is good for indoor use
  tft.fillScreen(TFT_BLACK);

  // ── Allocate JPEG buffer from 8 MB OPI PSRAM ─────────────────
  jpegBuf = (uint8_t*)ps_malloc(JPEG_BUF_SIZE);
  if (!jpegBuf) {
    // If PSRAM allocation fails — PSRAM may not be enabled in board settings
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    tft.drawString("FATAL: PSRAM alloc failed!", SCREEN_W / 2, SCREEN_H / 2 - 20);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Set PSRAM: OPI PSRAM in board settings", SCREEN_W / 2, SCREEN_H / 2 + 20);
    while (true) delay(1000);
  }
  Serial.printf("PSRAM JPEG buffer: %u KB allocated\n", JPEG_BUF_SIZE / 1024);

  // ── Connect to WiFi ───────────────────────────────────────────
  connectWiFi();
  Serial.printf("WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // ── Show idle screen ──────────────────────────────────────────
  drawIdleScreen();
  Serial.printf("Server base: %s\n", SERVER_BASE);
  Serial.println("Display ready. Polling server every 2 seconds...");
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  static unsigned long lastPollMs = 0;

  // ── Silent WiFi watchdog ──────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(3000);
    return;
  }

  // ── Touch Polling for Admit/Deny Buttons ──────────────────────
  if (isPending) {
    int32_t tx, ty;
    if (tft.getTouch(&tx, &ty)) {
      // Check if touch is within the vertical button band (y: 280 to 340)
      if (ty >= 280 && ty <= 340) {
        String action = "";
        if (tx >= 20 && tx <= 220) action = "ADMIT";
        else if (tx >= 260 && tx <= 460) action = "DENY";

        if (action != "") {
          // Send user's decision to the server
          HTTPClient http;
          http.begin(String(SERVER_BASE) + "/resolve_event");
          http.addHeader("Content-Type", "application/json");
          http.POST("{\"action\":\"" + action + "\"}");
          http.end();
          
          isPending = false;
          delay(500);         // Give server a moment to save
          checkForNewEvent(); // Force UI redraw (removes buttons, turns green/red)
        }
      }
    }
  }

  // ── Poll server on interval ───────────────────────────────────
  if (millis() - lastPollMs >= POLL_INTERVAL_MS) {
    lastPollMs = millis();
    checkForNewEvent();
  }
}

// ── Buzzer support (commented out — no buzzer in current test setup) ──────
//
// #define BUZZER_PIN  XX   // Pick a free GPIO on the sub-board connector
//                          // See Tab.2 in the datasheet: EXT_IO1..4
//                          // (check which aren't used by RS485 on your board)
//
// void chimeIndoor() {
//   tone(BUZZER_PIN, 1047, 200);   // C6
//   delay(250);
//   tone(BUZZER_PIN, 784,  400);   // G5
//   delay(500);
//   noTone(BUZZER_PIN);
// }
//
// Add  chimeIndoor();  inside checkForNewEvent(), immediately
// after  lastEventId = eventId;  and before fetchAndRenderImage().
