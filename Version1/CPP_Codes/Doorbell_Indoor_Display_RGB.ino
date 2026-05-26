#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <Arduino_GFX_Library.h>

// --- NETWORK CREDENTIALS ---
const char* ssid = "[WIFINAME]";
const char* password = "[PASSWORD]";
const char* websocket_server = "[IP_ADDRESS]"; 
const int websocket_port = 5050;
const char* image_url = "http://[IP_ADDRESS]:5050/latest_image.jpg";

WebSocketsClient webSocket;

// =========================================================================
// RGB PANEL HARDWARE CONFIGURATION (WT32S3-86S / Panlee)
// Note: These pins are standard for the WT32S3-86S, but if colors are inverted, 
// you may need to check the manufacturer's datasheet for the exact pinmap.
// =========================================================================
#define GFX_BL 45 // Backlight pin

Arduino_DataBus *bus = new Arduino_ESP32RGBPanel(
    39 /* DE */, 38 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
    5 /* R0 */, 45 /* R1 */, 48 /* R2 */, 47 /* R3 */, 21 /* R4 */,
    14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
    46 /* B0 */, 3 /* B1 */, 8 /* B2 */, 18 /* B3 */, 17 /* B4 */,
    1 /* hsync_polarity */, 40 /* hsync_front_porch */, 4 /* hsync_pulse_width */, 8 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 8 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 16000000 /* prefer_speed */);

Arduino_GFX *gfx = new Arduino_ST7701_RGBPanel(
    bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */,
    true /* IPS */, 480 /* width */, 480 /* height */,
    st7701_type1_init_operations /* init operations */, sizeof(st7701_type1_init_operations),
    true /* bgr */);
// =========================================================================

// Callback function required by TJpg_Decoder to push pixels to the Arduino_GFX display
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if ( y >= gfx->height() ) return 0;
  // Push the 16-bit RGB bitmap block to the screen
  gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
  return 1;
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected");
      break;
    case WStype_CONNECTED:
      Serial.println("[WS] Connected to Server");
      gfx->fillScreen(BLACK);
      gfx->setCursor(20, 20);
      gfx->setTextColor(WHITE);
      gfx->println("System Armed. Awaiting Visitors.");
      break;
    case WStype_TEXT:
      Serial.printf("[WS] Event Triggered: %s\n", payload);
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if(!error) {
        delay(500); // Allow Python time to save the JPEG
        
        HTTPClient http;
        http.begin(image_url);
        int httpCode = http.GET();
        
        if(httpCode == HTTP_CODE_OK) {
            int len = http.getSize();
            // The ESP32-S3 has plenty of PSRAM, we can allocate large buffers easily
            uint8_t * buff = (uint8_t *) heap_caps_malloc(len, MALLOC_CAP_SPIRAM); 
            if(buff) {
              WiFiClient * stream = http.getStreamPtr();
              size_t size = stream->readBytes(buff, len);
              
              // Draw the decoded image directly. 
              // Since the screen is 480x480 and the camera is likely 640x480 (VGA),
              // you may need to tweak TJpgDec.setJpgScale() or crop the image in Python.
              TJpgDec.drawJpg(0, 0, buff, size);
              free(buff);
            }
        }
        http.end();

        String status = doc["status"];
        String message = doc["message"];
        
        // Draw the text overlay at the bottom of the 480px screen
        gfx->fillRect(0, 420, gfx->width(), 60, BLACK);
        gfx->setCursor(20, 440);
        if(status == "SUCCESS") {
          gfx->setTextColor(GREEN);
        } else {
          gfx->setTextColor(RED);
        }
        gfx->println(message);
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);

  // Turn on the backlight physically
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  // Initialize the RGB Display
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE, BLACK);
  gfx->setTextSize(3); // Larger text for the 480p screen
  gfx->println("Booting Monitor...");

  // Setup JPEG Decoder
  TJpgDec.setJpgScale(1); // 1:1 scale.
  TJpgDec.setSwapBytes(true); 
  TJpgDec.setCallback(tft_output);

  WiFi.begin(ssid, password);
  gfx->println("Connecting WiFi...");
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  gfx->println("WiFi Connected.");
  
  webSocket.begin(websocket_server, websocket_port, "/socket.io/?EIO=4&transport=websocket");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop();
}