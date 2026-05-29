#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "fd_forward.h"
#include "fr_forward.h"

const char* ssid = "WIFINAME";
const char* password = "PASSWORD";
const char* serverUrl = "http://192.168.28.XXX:5050/edge_upload"; // Laptop IP

// Camera Pins
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

// --- NEW: Flash LED Pin ---
#define FLASH_LED_PIN     4

#ifndef FACE_WIDTH
#define FACE_WIDTH 56
#define FACE_HEIGHT 56
#endif

mtmn_config_t mtmn_config = {0};
face_id_list id_list = {0};
bool face_enrolled = false;

void setup() {
  // NEW: Wait for PC Serial Monitor to catch up
  delay(2000); 
  Serial.begin(115200);
  Serial.setDebugOutput(true); // Enables deep system crash logs
  
  // NEW: Initialize Flash LED
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW); // Keep it off by default

  Serial.println("\n--- BOOTING EDGE AI NODE ---");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n[✓] WiFi Connected");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_1;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA; 
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[X] Camera init failed: 0x%x\n", err);
    return;
  }
  Serial.println("[✓] Camera Initialized");

  mtmn_config.type = FAST;
  
  // FIX: Lowered from 80 to 40 so it detects faces further away
  mtmn_config.min_face = 40; 
  
  mtmn_config.pyramid = 0.707;
  mtmn_config.pyramid_times = 4;
  mtmn_config.p_threshold.score = 0.6;
  mtmn_config.p_threshold.nms = 0.7;
  mtmn_config.p_threshold.candidate_number = 20;
  mtmn_config.r_threshold.score = 0.7;
  mtmn_config.r_threshold.nms = 0.7;
  mtmn_config.r_threshold.candidate_number = 10;
  mtmn_config.o_threshold.score = 0.7;
  mtmn_config.o_threshold.nms = 0.7;
  mtmn_config.o_threshold.candidate_number = 1;

  face_id_init(&id_list, 5, 1); 
  Serial.println("[✓] Neural Network Ready.\n");
}

void loop() {
  // Print Memory Telemetry to monitor for leaks/crashes
  Serial.printf("\n--- NEW FRAME ---\nFree PSRAM: %d bytes\n", ESP.getFreePsram());
  
  // NEW: Pulse the flash LED for 50ms to signal a photo is being taken
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(50); 
  camera_fb_t * fb = esp_camera_fb_get();
  digitalWrite(FLASH_LED_PIN, LOW);
  
  if (!fb) { Serial.println("[X] Capture failed"); return; }

  dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
  if (!image_matrix) { 
    Serial.println("[X] Out of PSRAM!"); 
    esp_camera_fb_return(fb); 
    return; 
  }
  
  fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item);
  box_array_t *net_boxes = face_detect(image_matrix, &mtmn_config);
  
  String ai_result = "NO_FACE";
  String ai_name = "None";
  int box_x = 0, box_y = 0, box_w = 0, box_h = 0;

  if (net_boxes) {
    box_x = net_boxes->box[0].box_p[0];
    box_y = net_boxes->box[0].box_p[1];
    box_w = net_boxes->box[0].box_p[2] - box_x;
    box_h = net_boxes->box[0].box_p[3] - box_y;

    dl_matrix3du_t *aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
    
    // Safety check in case memory fragmented
    if (aligned_face && align_face(net_boxes, image_matrix, aligned_face) == ESP_OK) {
      if (!face_enrolled) {
        int8_t left_sample_num = enroll_face(&id_list, aligned_face);
        if (left_sample_num == 0) {
          face_enrolled = true;
          ai_result = "ENROLLED";
          ai_name = "Authorized_User";
          Serial.println("[AI] New Face Enrolled in RAM!");
        }
      } else {
        int8_t match_id = recognize_face(&id_list, aligned_face);
        if (match_id >= 0) {
          ai_result = "SUCCESS";
          ai_name = "Authorized_User";
          Serial.printf("[AI] Face Recognized! ID: %d\n", match_id);
        } else {
          ai_result = "DENIED";
          ai_name = "Intruder";
          Serial.println("[AI] Face NOT Recognized (Intruder).");
        }
      }
    }
    if (aligned_face) dl_matrix3du_free(aligned_face);
    dl_lib_free(net_boxes->score);
    dl_lib_free(net_boxes->box);
    dl_lib_free(net_boxes->landmark);
    dl_lib_free(net_boxes);
  } else {
    Serial.println("[AI] No face detected in frame.");
  }

  dl_matrix3du_free(image_matrix);

  WiFiClient client;
  HTTPClient http;
  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("X-AI-Result", ai_result);
  http.addHeader("X-AI-Name", ai_name);
  http.addHeader("X-Box-X", String(box_x));
  http.addHeader("X-Box-Y", String(box_y));
  http.addHeader("X-Box-W", String(box_w));
  http.addHeader("X-Box-H", String(box_h));

  int httpResponseCode = http.POST(fb->buf, fb->len);
  Serial.printf("[WIFI] Sent to Dashboard. HTTP Code: %d\n", httpResponseCode);
  
  http.end();
  esp_camera_fb_return(fb);

  Serial.println("Sleeping for 10 seconds...");
  delay(10000); 
}