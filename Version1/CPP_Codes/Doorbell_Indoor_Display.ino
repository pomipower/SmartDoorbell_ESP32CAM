#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>       // Display Library
#include <TJpg_Decoder.h>   // JPEG Decoder

const char* ssid = "[WIFINAME]";
const char* password = "[PASSWORD]";
const char* websocket_server = "[IP_ADDRESS]"; // IP only, no "http://"
const int websocket_port = 5050;
const char* image_url = "http://[IP_ADDRESS]:5050/latest_image.jpg";

TFT_eSPI tft = TFT_eSPI(); 
WebSocketsClient webSocket;

// Function required by TJpg_Decoder to push pixels to the screen
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if ( y >= tft.height() ) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected");
      break;
    case WStype_CONNECTED:
      Serial.println("[WS] Connected to Server");
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(10, 10);
      tft.setTextColor(TFT_WHITE);
      tft.println("System Armed. Awaiting Visitors.");
      break;
    case WStype_TEXT:
      Serial.printf("[WS] Event Triggered: %s\n", payload);
      
      // 1. Parse JSON WebSocket payload
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if(!error) {
        // Wait 500ms to ensure the Python script finishes saving the image file to disk
        delay(500); 
        
        // 2. Fetch the annotated image from the server
        HTTPClient http;
        http.begin(image_url);
        int httpCode = http.GET();
        
        if(httpCode == HTTP_CODE_OK) {
            int len = http.getSize();
            // Allocate a buffer in PSRAM/Heap for the JPEG stream
            uint8_t * buff = (uint8_t *) malloc(len);
            if(buff) {
              WiFiClient * stream = http.getStreamPtr();
              size_t size = stream->readBytes(buff, len);
              
              // Draw the image
              TJpgDec.drawJpg(0, 0, buff, size);
              free(buff);
            }
        }
        http.end();

        // 3. Draw UI text over the image
        String status = doc["status"];
        String message = doc["message"];
        
        tft.fillRect(0, tft.height() - 40, tft.width(), 40, TFT_BLACK);
        tft.setCursor(10, tft.height() - 30);
        if(status == "SUCCESS") {
          tft.setTextColor(TFT_GREEN);
        } else {
          tft.setTextColor(TFT_RED);
        }
        tft.println(message);
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize TFT Display
  tft.begin();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("Booting Monitor...");

  // Setup JPEG Decoder
  TJpgDec.setJpgScale(2); // Scales VGA (640x480) down to 320x240 for typical small TFTs. Adjust based on your provided screen.
  TJpgDec.setSwapBytes(true); // Required for TFT_eSPI
  TJpgDec.setCallback(tft_output);

  WiFi.begin(ssid, password);
  tft.println("Connecting WiFi...");
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  tft.println("WiFi Connected.");
  
  // Setup WebSocket Client
  webSocket.begin(websocket_server, websocket_port, "/socket.io/?EIO=4&transport=websocket");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop(); // Keep WebSocket connection alive and listen for events
}