// ============================================================
//  SMART DOORBELL — OUTDOOR UNIT (Version 2.1)
//  Board : AI-Thinker ESP32-CAM (OV2640 camera)
//  Role  : Capture visitor → POST to server → Handle Auth / Poll → Trigger Relay
//
//  V2.1 Behavior: 
//    If face is unknown, camera enters a 60-second polling loop 
//    checking /doorbell_status until the admin clicks Admit/Deny.
//
//  PIN SUMMARY (LCD removed, pins freed):
//    GPIO  1 (TX)  → Doorbell button (INPUT_PULLUP, after Serial.end())
//    GPIO 13       → Relay module IN  (Active-LOW, JQC3F-05VDC-C)
//    GPIO  4       → Onboard flash LED (status indicator, active-HIGH)
//    Camera        → GPIO 0,5,18,19,21,22,23,25,26,27,32,34,35,36,39
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ── STEP 1: Set your network credentials ─────────────────────
const char* WIFI_SSID     = "WIFINAME";
const char* WIFI_PASSWORD = "PASSWORD";

// ── STEP 2: Set your server's local IP (run `ipconfig` / `ip a`)
//            Keep port 5050 unless you changed it in the server
const char* SERVER_BASE = "http://[IP_ADDRESS]:5050"; // Update to your exact IP
// ─────────────────────────────────────────────────────────────

// ── Hardware pins ─────────────────────────────────────────────
#define BUTTON_PIN     1    // GPIO 1 (TX pin, freed after Serial.end())
#define RELAY_PIN     13    // GPIO 13 → Relay IN  (Active-LOW)
#define STATUS_LED_PIN 4    // GPIO 4  → Onboard white flash LED

// Relay timing
#define RELAY_UNLOCK_MS  3000   // Door stays unlocked for 3 seconds

// ── Camera pin definitions (AI-Thinker ESP32-CAM) ─────────────
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

// ─────────────────────────────────────────────────────────────
//  LED helpers
// ─────────────────────────────────────────────────────────────
void ledBlink(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(onMs);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(offMs);
  }
}

void ledSteady(int durationMs) {
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(durationMs);
  digitalWrite(STATUS_LED_PIN, LOW);
}

// ─────────────────────────────────────────────────────────────
//  Relay control  (Active-LOW: LOW = energise = door OPEN)
// ─────────────────────────────────────────────────────────────
void unlockDoor(int durationMs) {
  digitalWrite(RELAY_PIN, LOW);   // Pull to ground -> Relay ON
  delay(durationMs);
  digitalWrite(RELAY_PIN, HIGH);  // Pull to 3.3V -> Relay OFF
}

// ─────────────────────────────────────────────────────────────
//  Camera initialisation
// ─────────────────────────────────────────────────────────────
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_1;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;  // 640×480 — good for face_recognition accuracy
    config.jpeg_quality = 10;             // Lower = better quality (scale: 0–63)
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count     = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    return false;
  }

  // Sensor tuning for indoor/doorbell lighting
  sensor_t* s = esp_camera_sensor_get();
  s->set_brightness(s, 1);    // -2 to 2
  s->set_contrast(s, 1);      // -2 to 2
  s->set_saturation(s, 0);
  s->set_whitebal(s, 1);      // Auto white balance ON
  s->set_awb_gain(s, 1);
  s->set_exposure_ctrl(s, 1); // Auto exposure ON
  s->set_aec2(s, 1);          // AEC DSP ON

  return true;
}

// ─────────────────────────────────────────────────────────────
//  WiFi connection
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Slow pulse LED while connecting
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    ledBlink(1, 100, 400);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    // Failed: rapid blink forever — needs a power cycle
    while (true) ledBlink(1, 50, 50);
  }
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  // Brown-out detector off — camera draws heavy bursts of current
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // ── Relay: High-Impedance Hack for 3.3V/5V mismatch ──
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // 3.3V matching 3.3V = Relay completely OFF

  // ── Status LED ────────────────────────────────────────────────
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // ── Serial for boot diagnostics only ─────────────────────────
  Serial.begin(115200);
  Serial.println("\n\n=== Smart Doorbell - Outdoor Unit ===");

  // ── WiFi ─────────────────────────────────────────────────────
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  connectWiFi();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
  ledBlink(3, 80, 80); // 3 quick blinks = WiFi OK

  // ── Camera ───────────────────────────────────────────────────
  Serial.print("Initialising camera... ");
  if (!initCamera()) {
    Serial.println("FAILED. Halting.");
    while (true) ledBlink(5, 50, 50); // 5-blink error loop
  }
  Serial.println("OK");

  // ── Free GPIO 1 (TX) for button use ──────────────────────────
  // IMPORTANT: No Serial calls after this point in loop()
  Serial.println("Doorbell ready. Awaiting button press.");
  Serial.println("=====================================\n");
  Serial.end();

  // Configure button AFTER Serial.end() releases the pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {

  // ── WiFi watchdog (silent reconnect if dropped) ───────────────
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(3000);
    return;
  }

  // ── Wait for button press (active-LOW via INPUT_PULLUP) ────────
  if (digitalRead(BUTTON_PIN) != LOW) {
    delay(10);
    return;
  }

  // ─── DOORBELL PRESSED ────────────────────────────────────────

  // 3 short blinks: "I heard you, processing"
  ledBlink(3, 60, 60);

  // Discard the stale buffered frame (avoids showing a frame from
  // before the button was pressed — classic PSRAM DMA artefact)
  camera_fb_t* stale = esp_camera_fb_get();
  if (stale) esp_camera_fb_return(stale);

  // Brief pause for AE/AWB to settle on the new scene
  delay(250);

  // Capture fresh frame
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    ledBlink(6, 40, 40); // Rapid blink = capture failed
    delay(2000);
    // Wait for button release before returning
    while (digitalRead(BUTTON_PIN) == LOW) delay(10);
    return;
  }

  // POST JPEG to Flask server
  WiFiClient wifiClient;
  wifiClient.setTimeout(20000);

  HTTPClient http;
  // Construct URL inline safely after memory is initialized
  http.begin(wifiClient, String(SERVER_BASE) + "/doorbell_ring");
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(20000); // 20s — inference can be slow

  int httpCode = http.POST(fb->buf, fb->len);
  esp_camera_fb_return(fb); // Return buffer immediately after POST

  if (httpCode == 200) {
    String response = http.getString();
    http.end(); // CRITICAL: Close main connection to free the socket for polling!

    if (response.startsWith("Welcome")) {
      // ── ACCESS GRANTED ────────────────────────────────────────
      unlockDoor(RELAY_UNLOCK_MS);
      ledSteady(300);

    } 
    else if (response.startsWith("ACTION REQUIRED")) {
      // Unknown Face -> Enter 60-second waiting loop
      unsigned long startWait = millis();
      bool resolved = false;
      
      while (millis() - startWait < 60000) { // Wait up to 60 seconds
        HTTPClient pollHttp;
        // Construct polling URL inline safely
        pollHttp.begin(wifiClient, String(SERVER_BASE) + "/doorbell_status");
        int c = pollHttp.GET();
        
        if (c == 200) {
          String stat = pollHttp.getString();
          if (stat == "MANUAL_ADMIT") {
            unlockDoor(RELAY_UNLOCK_MS);
            ledSteady(300);
            resolved = true;
            break;
          } else if (stat == "MANUAL_DENY") {
            ledBlink(4, 80, 80);
            resolved = true;
            break;
          }
        }
        pollHttp.end();
        delay(1000); // Poll every 1 second
      }
      if (!resolved) ledBlink(4, 80, 80); // Timeout (No admin responded)
    } 
    else {
      // ── ACCESS DENIED or SCAN FAILED ─────────────────────────
      ledBlink(4, 80, 80); 
    }
  } 
  else {
    // Server error or unreachable
    http.end(); // Ensure closed on error
    ledBlink(2, 200, 200); 
  }
  
  // Debounce: wait for button to be physically released
  while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
  delay(500); // Extra settle time before next press is accepted
}


// ── Buzzer (commented out — no buzzer in current test setup) ──
//
// #define BUZZER_PIN 2
//
// void doorbellChime() {
//   // Simple two-tone doorbell chime using tone()
//   tone(BUZZER_PIN, 1047, 200);  // C6
//   delay(220);
//   tone(BUZZER_PIN, 784, 400);   // G5
//   delay(450);
//   noTone(BUZZER_PIN);
// }
//
// Call doorbellChime() at the start of the button-press handler
// (before the capture) to give immediate audible feedback.
