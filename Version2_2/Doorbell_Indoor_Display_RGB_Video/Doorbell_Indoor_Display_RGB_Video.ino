// ==============================================================================
//  SMART DOORBELL — INDOOR DISPLAY (PANLEE BC02)
//  Framework: PanelLan + LovyanGFX + WebSockets Ping-Pong Streaming
// ==============================================================================

#include <WiFi.h>
#include <WebSocketsClient.h>
#include "PanelLan.h"

// ── Configuration ─────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "WIFINAME";
const char* WIFI_PASS     = "PASSWORD";

const char* WS_HOST       = "10.151.110.250";
const uint16_t WS_PORT    = 5050;
const char* WS_PATH       = "/ws/frontend";

// ── Hardware Globals ──────────────────────────────────────────────────────────
PanelLan tft(BOARD_BC02);     // Automatically configures BC02 RGB/SPI pins!
WebSocketsClient webSocket;
SemaphoreHandle_t tftMutex;   // Protects display from simultaneous core writes

// ── Ping-Pong Video Buffers ───────────────────────────────────────────────────
#define MAX_JPEG_SIZE 102400  // 100 KB max per frame
uint8_t* rx_buffer[2];        // Dual buffers stored in 8MB PSRAM
volatile size_t rx_size[2]    = {0, 0};
volatile uint8_t write_idx    = 0;
volatile int8_t read_idx      = -1;
TaskHandle_t decodeTaskHandle = NULL;

// ── Background JPEG Decoder Task (Core 0) ─────────────────────────────────────
void decodeTask(void* parameter) {
    while (true) {
        // Sleep until the WebSocket thread wakes us up with a full frame
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (read_idx >= 0) {
            uint8_t idx = read_idx; 
            
            if (xSemaphoreTake(tftMutex, portMAX_DELAY)) {
                // Use LovyanGFX's built-in Hardware JPEG Decoder via PanelLan
                // drawJpg(data, length, x, y, maxWidth, maxHeight, offX, offY, scaleX, scaleY)
                // Scaling 320x240 up by 1.5x gives 480x360. Y=60 centers it on a 480x480 screen.
                tft.drawJpg(rx_buffer[idx], rx_size[idx], 0, 60, 0, 0, 0, 0, 1.5f, 1.5f);
                xSemaphoreGive(tftMutex);
            }
        }
    }
}

// ── WebSocket Data Handler (Core 1) ───────────────────────────────────────────
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected!");
            if (xSemaphoreTake(tftMutex, portMAX_DELAY)) {
                tft.fillRect(0, 440, 480, 40, TFT_BLACK); // Clear bottom bar
                tft.setTextColor(TFT_RED, TFT_BLACK);
                tft.setTextDatum(textdatum_t::middle_center);
                tft.drawString("Connection Lost. Retrying...", 240, 460);
                xSemaphoreGive(tftMutex);
            }
            break;
            
        case WStype_CONNECTED:
            Serial.println("[WS] Connected!");
            if (xSemaphoreTake(tftMutex, portMAX_DELAY)) {
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_GREEN, TFT_BLACK);
                tft.setTextDatum(textdatum_t::middle_center);
                tft.drawString("Connected! Streaming...", 240, 460);
                xSemaphoreGive(tftMutex);
            }
            break;
            
        case WStype_BIN:
            // Frame received! Write to Ping-Pong buffer safely.
            if (length > 0 && length <= MAX_JPEG_SIZE) {
                memcpy(rx_buffer[write_idx], payload, length);
                rx_size[write_idx] = length;
                
                // Swap buffers
                read_idx = write_idx;
                write_idx = (write_idx + 1) % 2;
                
                // Wake up the decoder task!
                if (decodeTaskHandle != NULL) {
                    xTaskNotifyGive(decodeTaskHandle);
                }
            } else {
                Serial.printf("[WS Error] Frame rejected. Size: %u bytes\n", length);
            }
            break;
            
        // Ignore JSON text payloads and ping/pongs for now
        case WStype_TEXT:
        case WStype_PING:
        case WStype_PONG:
            break;
    }
}

// ── Main Setup ────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Booting Smart Doorbell Display ===");

    tftMutex = xSemaphoreCreateMutex();

    // 1. Initialize Display
    tft.begin();
    tft.setRotation(0);
    tft.setBrightness(200);
    tft.fillScreen(TFT_BLACK);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.drawString("Booting Smart Doorbell...", 240, 240);

    // 2. Allocate PSRAM
    Serial.println("Allocating PSRAM Buffers...");
    rx_buffer[0] = (uint8_t*)ps_malloc(MAX_JPEG_SIZE);
    rx_buffer[1] = (uint8_t*)ps_malloc(MAX_JPEG_SIZE);
    
    if (!rx_buffer[0] || !rx_buffer[1]) {
        Serial.println("FATAL: PSRAM Allocation failed!");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("FATAL ERROR: PSRAM Alloc Failed.", 240, 220);
        tft.drawString("Check 'OPI PSRAM' in settings.", 240, 260);
        while(1) delay(100);
    }

    // 3. Start Dedicated Decoder Task on Core 0
    xTaskCreatePinnedToCore(
        decodeTask, "DecodeTask", 8192, NULL, 1, &decodeTaskHandle, 0
    );

    // 4. Connect to Wi-Fi
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Connecting to Wi-Fi...", 240, 240);
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\n[WIFI] Connected!");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Wi-Fi Connected!", 240, 220);
    tft.drawString(WiFi.localIP().toString(), 240, 260);
    
    delay(1500); 

    // 5. Connect to WebSocket
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Connecting to FastAPI Server...", 240, 240);
    
    webSocket.begin(WS_HOST, WS_PORT, WS_PATH);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000); // Auto-reconnect if Python drops
}

// ── Main Loop (Core 1) ────────────────────────────────────────────────────────
void loop() {
    // This loop handles all network traffic instantly. 
    // It automatically replies to Python Server Pings in the background!
    webSocket.loop();
}