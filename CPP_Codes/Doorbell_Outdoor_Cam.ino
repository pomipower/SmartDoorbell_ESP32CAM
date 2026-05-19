#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <ArduinoJson.h> // Ensure you installed this library!
#include "soc/soc.h"           
#include "soc/rtc_cntl_reg.h"  

const char* ssid = "[WIFINAME]";
const char* password = "[PASSWORD]";
const char* serverName = "http://[IP_ADDRESS]:5050/clock_in";

// === PIN SETUP ===
const int BUTTON_PIN = 13; // Moved to a safe RTC pin
const int RELAY_PIN = 14;  // Active-LOW Relay
// const int BUZZER_PIN = 12; // Future Implementation

// Camera Pins (OV2640/OV3360 standard)
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Secure the Relay immediately on boot
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // HIGH keeps Active-Low relay OFF

  /* Future Buzzer setup
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  */

  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected.");

  // Setup Camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_1;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_camera_init(&config);
  Serial.println("Outdoor node ready.");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Doorbell Rung! Capturing...");
    
    // Clear stale buffer
    camera_fb_t * stale_fb = esp_camera_fb_get();
    if (stale_fb) esp_camera_fb_return(stale_fb);

    delay(200); // AE/AWB adjustment time

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera Capture Failed");
      delay(1000);
      return;
    }

    // Send Image to Server
    WiFiClient client;
    HTTPClient http;
    http.begin(client, serverName);
    http.addHeader("Content-Type", "image/jpeg"); 
    
    int httpResponseCode = http.POST(fb->buf, fb->len);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Server Response: " + response);

      // Parse JSON
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      
      if (!error) {
        String status = doc["status"];
        
        if (status == "SUCCESS") {
          Serial.println("Access Granted. Triggering Relay...");
          
          /* Future Buzzer logic for success
          digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW); delay(100);
          digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW);
          */

          // Unlock Door
          digitalWrite(RELAY_PIN, LOW); // Trigger active-low relay
          delay(3000);                  // Hold door open for 3 seconds
          digitalWrite(RELAY_PIN, HIGH);// Lock Door
        } else {
          Serial.println("Access Denied. Intruder.");
          /* Future Buzzer logic for denied (long beep)
          digitalWrite(BUZZER_PIN, HIGH); delay(1000); digitalWrite(BUZZER_PIN, LOW);
          */
        }
      }
    } else {
      Serial.println("Network Error on POST");
    }

    http.end();
    esp_camera_fb_return(fb);
    
    // Debounce wait
    while(digitalRead(BUTTON_PIN) == LOW) { delay(10); } 
  }
}
