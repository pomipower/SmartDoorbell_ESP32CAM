// ==============================================================================
//  SMART DOORBELL V2 — OFFLINE AI PROOF-OF-CONCEPT
//  Hardware: ESP32-CAM (OV3660)
//  Task: Capture -> Detect Face -> Extract Embedding -> Recognize/Enroll
// ==============================================================================

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "mbedtls/base64.h"

// ── Espressif Deep Learning Framework (esp-dl) ────────────────────────────────
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#include "human_face_detect_msr01.hpp"
#include "face_recognition_112_v1_s8.hpp"
#pragma GCC diagnostic pop

// ── ESP-DL LINKER BUG FIX ─────────────────────────────────────────────────────
namespace dl {
    namespace image {
        template <typename T>
        void warp_affine(dl::Tensor<T> *destination, dl::Tensor<T> *input, dl::math::Matrix<float> *M);

        template <>
        void warp_affine<short>(dl::Tensor<short> *destination, dl::Tensor<short> *input, dl::math::Matrix<float> *M) {}
    }
}
// ──────────────────────────────────────────────────────────────────────────────

static const char *TAG = "AI_POC";

// ── Hardware Pins ─────────────────────────────────────────────────────────────
#define BUTTON_PIN      GPIO_NUM_12  
#define FLASH_LED_PIN   GPIO_NUM_4

// OV3660 Camera Pins
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1 
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_Y9      35
#define CAM_PIN_Y8      34
#define CAM_PIN_Y7      39
#define CAM_PIN_Y6      36
#define CAM_PIN_Y5      21
#define CAM_PIN_Y4      19
#define CAM_PIN_Y3      18
#define CAM_PIN_Y2      5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

// ── Global AI Models ──────────────────────────────────────────────────────────
HumanFaceDetectMSR01 s1(0.4F, 0.3F, 1, 1.0F); 
FaceRecognition112V1S8 recognizer;

void init_hardware() {
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY); 

    gpio_reset_pin(FLASH_LED_PIN);
    gpio_set_direction(FLASH_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASH_LED_PIN, 0);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = CAM_PIN_Y2;
    config.pin_d1       = CAM_PIN_Y3;
    config.pin_d2       = CAM_PIN_Y4;
    config.pin_d3       = CAM_PIN_Y5;
    config.pin_d4       = CAM_PIN_Y6;
    config.pin_d5       = CAM_PIN_Y7;
    config.pin_d6       = CAM_PIN_Y8;
    config.pin_d7       = CAM_PIN_Y9;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.xclk_freq_hz = 15000000;
    
    config.pixel_format = PIXFORMAT_RGB565; 
    config.frame_size   = FRAMESIZE_QVGA; 
    config.jpeg_quality = 12;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_PSRAM;

    ESP_ERROR_CHECK(esp_camera_init(&config));

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 1);   
        s->set_hmirror(s, 1); 
        ESP_LOGI(TAG, "Hardware 180-degree rotation applied.");
    }

    ESP_LOGI(TAG, "Camera & Hardware Initialized.");
}

// ── MAIN EXECUTION ────────────────────────────────────────────────────────────
#ifdef __cplusplus
extern "C" {
#endif

void app_main(void) {
    init_hardware();
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "🤖 AI Engine Ready (State Machine Active).");
    ESP_LOGI(TAG, "👉 Press the button on GPIO 12 to capture.");
    ESP_LOGI(TAG, "==================================================");

    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) { 
            ESP_LOGI(TAG, "📸 Button pressed! Firing flash and adjusting exposure...");
            
            gpio_set_level(FLASH_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(400)); 

            camera_fb_t *fb = esp_camera_fb_get();
            if (fb) esp_camera_fb_return(fb); 
            
            fb = esp_camera_fb_get(); 
            gpio_set_level(FLASH_LED_PIN, 0); 

            if (!fb) {
                ESP_LOGE(TAG, "❌ Camera Capture Failed! Framebuffer is null.");
                continue; 
            }

            // ── DEBUG: PRINT IMAGE TO SERIAL ────────────────────────────────────
            ESP_LOGI(TAG, "Generating Base64 image for debugging...");
            uint8_t *jpeg_buf = NULL;
            size_t jpeg_len = 0;
            if (frame2jpg(fb, 80, &jpeg_buf, &jpeg_len)) {
                size_t b64_len = 0;
                mbedtls_base64_encode(NULL, 0, &b64_len, jpeg_buf, jpeg_len);
                unsigned char *b64_buf = (unsigned char *)heap_caps_malloc(b64_len + 1, MALLOC_CAP_SPIRAM);
                
                if (b64_buf) {
                    size_t olen = 0;
                    mbedtls_base64_encode(b64_buf, b64_len, &olen, jpeg_buf, jpeg_len);
                    b64_buf[olen] = '\0';
                    
                    printf("\n=== COPY EVERYTHING BETWEEN THESE LINES ===\n");
                    for (size_t i = 0; i < olen; i += 1024) {
                        printf("%.*s", (int)(olen - i > 1024 ? 1024 : olen - i), b64_buf + i);
                    }
                    printf("\n===========================================\n\n");
                    free(b64_buf);
                }
                free(jpeg_buf);
            }
            // ──────────────────────────────────────────────────────────────────

            int64_t start_time = esp_timer_get_time();
            ESP_LOGI(TAG, "🖼️ Fresh frame captured (%dx%d). Allocating AI Tensor Buffer...", fb->width, fb->height);

            uint8_t *rgb888_buf = (uint8_t *)heap_caps_malloc(fb->width * fb->height * 3, MALLOC_CAP_SPIRAM);
            if (!rgb888_buf) {
                ESP_LOGE(TAG, "❌ Failed to allocate RGB888 buffer in PSRAM.");
                esp_camera_fb_return(fb);
                continue;
            }
            fmt2rgb888(fb->buf, fb->len, fb->format, rgb888_buf);

            // ── AI PIPELINE: DETECT, CROP, AND RESIZE ───────────────────────
            ESP_LOGI(TAG, "🔍 Running Face Box Detector (MSR01)...");
            auto candidates = s1.infer(rgb888_buf, {(int)fb->height, (int)fb->width, 3});

            if (candidates.size() > 0) {
                float confidence = candidates.front().score;
                ESP_LOGI(TAG, "🎯 Face Box Found! Confidence: %.2f%%", confidence * 100.0);

                int x1 = candidates.front().box[0];
                int y1 = candidates.front().box[1];
                int x2 = candidates.front().box[2];
                int y2 = candidates.front().box[3];

                if (x1 < 0) x1 = 0;
                if (y1 < 0) y1 = 0;
                if (x2 >= fb->width) x2 = fb->width - 1;
                if (y2 >= fb->height) y2 = fb->height - 1;

                int face_w = x2 - x1 + 1;
                int face_h = y2 - y1 + 1;

                if (face_w > 20 && face_h > 20) {
                    ESP_LOGI(TAG, "👤 Face is clear! Aligning and Preparing Tensor...");
                    
                    uint8_t *aligned_buf = (uint8_t *)heap_caps_aligned_alloc(16, 112 * 112 * 3, MALLOC_CAP_SPIRAM);
                    if (aligned_buf) {
                        for (int y = 0; y < 112; y++) {
                            for (int x = 0; x < 112; x++) {
                                int src_x = x1 + (x * face_w) / 112;
                                int src_y = y1 + (y * face_h) / 112;
                                
                                if (src_x > x2) src_x = x2;
                                if (src_y > y2) src_y = y2;
                                
                                int src_idx = (src_y * fb->width + src_x) * 3;
                                int dst_idx = (y * 112 + x) * 3;
                                
                                aligned_buf[dst_idx]     = rgb888_buf[src_idx];
                                aligned_buf[dst_idx + 1] = rgb888_buf[src_idx + 1];
                                aligned_buf[dst_idx + 2] = rgb888_buf[src_idx + 2];
                            }
                        }

                        dl::Tensor<uint8_t> aligned_tensor;
                        aligned_tensor.set_element(aligned_buf);
                        aligned_tensor.set_shape({112, 112, 3});
                        aligned_tensor.set_auto_free(false); 

                        ESP_LOGI(TAG, "🧠 Running MobileFaceNet...");

                        // ── ENROLLMENT VS RECOGNITION LOGIC ────────────────────────
                        int enrolled_count = recognizer.get_enrolled_id_num();

                        if (enrolled_count == 0) {
                            // DATABASE IS EMPTY: Enroll the first face
                            ESP_LOGI(TAG, "📝 Database is empty. Enrolling as New User...");
                            int enrolled_id = recognizer.enroll_id(aligned_tensor, "test_user", false);
                            
                            int64_t end_time = esp_timer_get_time();
                            ESP_LOGI(TAG, "==================================================");
                            ESP_LOGI(TAG, "✅ ENROLLMENT SUCCESS!");
                            ESP_LOGI(TAG, "⏱️ Processing Time: %lld ms", (end_time - start_time) / 1000);
                            ESP_LOGI(TAG, "🆔 Saved as Face ID: %d", enrolled_id);
                            ESP_LOGI(TAG, "==================================================");
                        } else {
                            // DATABASE HAS FACES: Attempt to recognize the live face
                            ESP_LOGI(TAG, "🔍 Database has %d user(s). Attempting Recognition...", enrolled_count);
                            auto recognize_result = recognizer.recognize(aligned_tensor);
                            
                            int64_t end_time = esp_timer_get_time();
                            ESP_LOGI(TAG, "==================================================");
                            ESP_LOGI(TAG, "✅ RECOGNITION COMPLETE!");
                            ESP_LOGI(TAG, "⏱️ Processing Time: %lld ms", (end_time - start_time) / 1000);
                            
                            if (recognize_result.id > 0) {
                                ESP_LOGI(TAG, "🔓 MATCH FOUND! Welcome back, Face ID: %d", recognize_result.id);
                                ESP_LOGI(TAG, "📊 Similarity Score: %.4f", recognize_result.similarity);
                            } else {
                                ESP_LOGW(TAG, "🚫 INTRUDER! Face not recognized.");
                                ESP_LOGW(TAG, "📊 Highest Similarity was only: %.4f", recognize_result.similarity);
                            }
                            ESP_LOGI(TAG, "==================================================");
                        }
                        // ───────────────────────────────────────────────────────────

                        free(aligned_buf);
                    } else {
                        ESP_LOGE(TAG, "❌ Failed to allocate memory for aligned face.");
                    }
                } else {
                    ESP_LOGW(TAG, "⚠️ Face box is too small to process.");
                }
            } else {
                ESP_LOGW(TAG, "👻 No face detected in the frame.");
            }

            free(rgb888_buf);
            esp_camera_fb_return(fb);

            ESP_LOGI(TAG, "⏳ Cooling down for 1 second...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGI(TAG, "👉 Ready for next capture.");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

#ifdef __cplusplus
}
#endif