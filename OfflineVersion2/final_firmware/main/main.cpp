// ==============================================================================
//  SMART DOORBELL V2 — FINAL FIRMWARE
//  Hardware: ESP32-CAM (OV3660)
//  Task: AI Pipeline + Wi-Fi + 2-Way WebSockets + Admin Control State Machine
// ==============================================================================

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "mbedtls/base64.h"
#include "nvs_flash.h" 
#include "esp_partition.h"

// Networking Headers
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_websocket_client.h"
#include "cJSON.h"

// ── USER CONFIGURATION (CHANGE THESE!) ────────────────────────────────────────
#define WIFI_SSID       "Galaxy S24"
#define WIFI_PASS       "qwne3344"
#define WEBSOCKET_URI   "ws://172.28.199.250:8000/ws/esp32" // Put your PC's IP here!
// ──────────────────────────────────────────────────────────────────────────────

// ── Espressif Deep Learning Framework ─────────────────────────────────────────
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#include "human_face_detect_msr01.hpp"
#include "face_recognition_112_v1_s8.hpp"
#pragma GCC diagnostic pop

namespace dl {
    namespace image {
        template <typename T> void warp_affine(dl::Tensor<T> *destination, dl::Tensor<T> *input, dl::math::Matrix<float> *M);
        template <> void warp_affine<short>(dl::Tensor<short> *destination, dl::Tensor<short> *input, dl::math::Matrix<float> *M) {}
    }
}

static const char *TAG = "FW_FINAL";

// Hardware Pins
#define BUTTON_PIN      GPIO_NUM_12  
#define FLASH_LED_PIN   GPIO_NUM_4
#define RELAY_PIN       GPIO_NUM_13  
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

// Global Variables & State Machine
HumanFaceDetectMSR01 *s1 = nullptr; 
FaceRecognition112V1S8 *recognizer = nullptr;
esp_websocket_client_handle_t ws_client = nullptr;

bool is_registration_mode = false; 

// ── EVENT GROUPS FOR CLEAN STARTUP ──
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// ── TASK: UNLOCK DOOR (Non-Blocking) ──
void unlock_door_task(void *pvParameters) {
    ESP_LOGI(TAG, "🚪 Triggering Relay (Unlocking Door)...");
    gpio_set_level(RELAY_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(5000)); 
    gpio_set_level(RELAY_PIN, 0);
    ESP_LOGI(TAG, "🚪 Relay Off (Door Locked).");
    vTaskDelete(NULL); 
}

// ── TASK: START WEBSOCKET AFTER IP ──
void start_ws_task(void *pvParameters) {
    // Waits silently in the background until Wi-Fi is connected
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "🌐 IP Address Acquired! Starting WebSocket Client cleanly...");
    esp_websocket_client_start(ws_client);
    vTaskDelete(NULL); // Destroy task once connected to save memory
}

// ── HELPER: BROADCAST STATE/PAYLOAD ──
void broadcast_ws_payload(const char* status, int id, float similarity, unsigned char* b64_image) {
    if (!esp_websocket_client_is_connected(ws_client)) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddNumberToObject(root, "similarity", similarity);
    cJSON_AddNumberToObject(root, "total_users", recognizer ? recognizer->get_enrolled_id_num() : 0);
    cJSON_AddBoolToObject(root, "registration_mode", is_registration_mode);
    
    if (b64_image) cJSON_AddStringToObject(root, "image", (const char*)b64_image);

    char *json_string = cJSON_PrintUnformatted(root);
    if (json_string) {
        esp_websocket_client_send_text(ws_client, json_string, strlen(json_string), portMAX_DELAY);
        free(json_string);
    }
    cJSON_Delete(root);
}

// ── WI-FI EVENT HANDLER ──
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected. Retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "🟢 Wi-Fi Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        // Signal the background task to start the WebSocket
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ── WEBSOCKET EVENT HANDLER (JSON COMMAND PARSER) ──
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "🟢 WebSocket Connected to Server!");
        broadcast_ws_payload("SYSTEM ONLINE", -1, 0, NULL); 
    } else if (event_id == WEBSOCKET_EVENT_DATA && data->op_code == 0x01 && data->data_len > 0) {
        
        char *received_text = (char*)malloc(data->data_len + 1);
        memcpy(received_text, data->data_ptr, data->data_len);
        received_text[data->data_len] = '\0';
        ESP_LOGI(TAG, "📥 Command Received from Server: %s", received_text);

        cJSON *json = cJSON_Parse(received_text);
        if (json) {
            cJSON *cmd = cJSON_GetObjectItem(json, "cmd");
            if (cmd && cJSON_IsString(cmd)) {
                
                if (strcmp(cmd->valuestring, "set_mode") == 0) {
                    cJSON *mode = cJSON_GetObjectItem(json, "mode");
                    if (mode && cJSON_IsString(mode)) {
                        is_registration_mode = (strcmp(mode->valuestring, "registration") == 0);
                        ESP_LOGI(TAG, "⚙️ Mode changed to: %s", is_registration_mode ? "REGISTRATION" : "SECURITY");
                        broadcast_ws_payload("MODE CHANGED", -1, 0, NULL);
                    }
                } 
                else if (strcmp(cmd->valuestring, "unlock_door") == 0) {
                    xTaskCreate(unlock_door_task, "unlock_door_task", 2048, NULL, 5, NULL);
                    broadcast_ws_payload("DOOR MANUALLY UNLOCKED", -1, 0, NULL);
                } 
                else if (strcmp(cmd->valuestring, "delete_user") == 0) {
                    if (recognizer && recognizer->get_enrolled_id_num() > 0) {
                        recognizer->delete_id(true); 
                        ESP_LOGI(TAG, "🗑️ Deleted last user. Remaining: %d", recognizer->get_enrolled_id_num());
                        broadcast_ws_payload("USER DELETED", -1, 0, NULL);
                    }
                }
            }
            cJSON_Delete(json);
        }
        free(received_text);
    }
}

void init_hardware_and_network() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY); 
    
    gpio_reset_pin(FLASH_LED_PIN);
    gpio_set_direction(FLASH_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASH_LED_PIN, 0);

    gpio_reset_pin(RELAY_PIN);
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_PIN, 0); 

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
    if (s) { s->set_vflip(s, 1); s->set_hmirror(s, 1); }
    
    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = WEBSOCKET_URI;
    websocket_cfg.reconnect_timeout_ms = 2000; 
    websocket_cfg.network_timeout_ms = 3000;   
    
    ws_client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)ws_client);
    
    // Spawn the background task to wait for Wi-Fi and start the WebSocket
    xTaskCreate(start_ws_task, "start_ws_task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Hardware, Wi-Fi, and WebSockets Initialized.");
}

#ifdef __cplusplus
extern "C" {
#endif

void app_main(void) {
    init_hardware_and_network();

    s1 = new HumanFaceDetectMSR01(0.4F, 0.3F, 1, 1.0F); 
    recognizer = new FaceRecognition112V1S8();
    recognizer->set_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fr");
    recognizer->set_ids_from_flash(); 

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "🤖 FIRMWARE V2.0 (STATE MACHINE ACTIVE)");
    ESP_LOGI(TAG, "💾 Faces currently loaded: %d", recognizer->get_enrolled_id_num());
    ESP_LOGI(TAG, "==================================================");

    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) { 
            int press_duration_ms = 0;
            while(gpio_get_level(BUTTON_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                press_duration_ms += 10;
            }

            if (press_duration_ms > 3000) {
                ESP_LOGW(TAG, "🚨 LONG PRESS: Erasing Database...");
                while(recognizer->get_enrolled_id_num() > 0) recognizer->delete_id(true); 
                ESP_LOGI(TAG, "✅ Database Erased.");
                broadcast_ws_payload("DATABASE ERASED", -1, 0.0, NULL);
                
                for(int i=0; i<3; i++) {
                    gpio_set_level(FLASH_LED_PIN, 1); vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_set_level(FLASH_LED_PIN, 0); vTaskDelay(pdMS_TO_TICKS(100));
                }
                continue; 
            } 
            else if (press_duration_ms > 50) {
                ESP_LOGI(TAG, "📸 Capturing...");
                gpio_set_level(FLASH_LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(400)); 

                camera_fb_t *fb = esp_camera_fb_get();
                if (fb) esp_camera_fb_return(fb); 
                fb = esp_camera_fb_get(); 
                gpio_set_level(FLASH_LED_PIN, 0); 

                if (!fb) continue; 

                unsigned char *b64_buf = NULL;
                uint8_t *jpeg_buf = NULL;
                size_t jpeg_len = 0;
                if (frame2jpg(fb, 80, &jpeg_buf, &jpeg_len)) {
                    size_t b64_len = 0;
                    mbedtls_base64_encode(NULL, 0, &b64_len, jpeg_buf, jpeg_len);
                    b64_buf = (unsigned char *)heap_caps_malloc(b64_len + 1, MALLOC_CAP_SPIRAM);
                    if (b64_buf) {
                        size_t olen = 0;
                        mbedtls_base64_encode(b64_buf, b64_len, &olen, jpeg_buf, jpeg_len);
                        b64_buf[olen] = '\0';
                    }
                    free(jpeg_buf);
                }

                uint8_t *rgb888_buf = (uint8_t *)heap_caps_malloc(fb->width * fb->height * 3, MALLOC_CAP_SPIRAM);
                if (!rgb888_buf) {
                    esp_camera_fb_return(fb);
                    if (b64_buf) free(b64_buf);
                    continue;
                }
                fmt2rgb888(fb->buf, fb->len, fb->format, rgb888_buf);

                ESP_LOGI(TAG, "🔍 Processing AI Pipeline...");
                auto candidates = s1->infer(rgb888_buf, {(int)fb->height, (int)fb->width, 3});

                if (candidates.size() > 0) {
                    int x1 = candidates.front().box[0]; int y1 = candidates.front().box[1];
                    int x2 = candidates.front().box[2]; int y2 = candidates.front().box[3];

                    if (x1 < 0) x1 = 0; 
                    if (y1 < 0) y1 = 0;
                    if (x2 >= fb->width) x2 = fb->width - 1; 
                    if (y2 >= fb->height) y2 = fb->height - 1;

                    int face_w = x2 - x1 + 1;
                    int face_h = y2 - y1 + 1;

                    if (face_w > 20 && face_h > 20) {
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
                                    aligned_buf[dst_idx] = rgb888_buf[src_idx];
                                    aligned_buf[dst_idx + 1] = rgb888_buf[src_idx + 1];
                                    aligned_buf[dst_idx + 2] = rgb888_buf[src_idx + 2];
                                }
                            }

                            dl::Tensor<uint8_t> aligned_tensor;
                            aligned_tensor.set_element(aligned_buf);
                            aligned_tensor.set_shape({112, 112, 3});
                            aligned_tensor.set_auto_free(false); 

                            // ── STATE MACHINE EXECUTION ──
                            if (is_registration_mode) {
                                bool already_exists = false;
                                
                                // Pre-check: Does this person already exist in the database?
                                if (recognizer->get_enrolled_id_num() > 0) {
                                    auto check_result = recognizer->recognize(aligned_tensor);
                                    if (check_result.id > 0) {
                                        already_exists = true;
                                        ESP_LOGW(TAG, "⚠️ Already registered as User %d (Sim: %.4f)", check_result.id, check_result.similarity);
                                        broadcast_ws_payload("ALREADY_REGISTERED", check_result.id, check_result.similarity, b64_buf);
                                    }
                                }

                                // If not recognized, go ahead and enroll them!
                                if (!already_exists) {
                                    int enrolled_id = recognizer->enroll_id(aligned_tensor, "user", true);
                                    ESP_LOGI(TAG, "✅ ENROLLED USER: %d", enrolled_id);
                                    broadcast_ws_payload("ENROLLED", enrolled_id, 1.0, b64_buf);
                                }
                            } else {
                                if (recognizer->get_enrolled_id_num() > 0) {
                                    auto result = recognizer->recognize(aligned_tensor);
                                    
                                    if (result.id > 0) {
                                        ESP_LOGI(TAG, "🔓 MATCH! ID: %d | Sim: %.4f", result.id, result.similarity);
                                        xTaskCreate(unlock_door_task, "unlock_door_task", 2048, NULL, 5, NULL); // UNLOCK!
                                        broadcast_ws_payload("MATCH", result.id, result.similarity, b64_buf);
                                    } else {
                                        ESP_LOGW(TAG, "🚫 INTRUDER! Sim: %.4f", result.similarity);
                                        broadcast_ws_payload("INTRUDER", -1, result.similarity, b64_buf);
                                    }
                                } else {
                                    ESP_LOGW(TAG, "⚠️ Security Mode Active, but Database is Empty!");
                                    broadcast_ws_payload("DB_EMPTY", -1, 0.0, b64_buf);
                                }
                            }
                            free(aligned_buf);
                        }
                    } else {
                        ESP_LOGW(TAG, "⚠️ Face too small.");
                        broadcast_ws_payload("FACE_TOO_SMALL", -1, 0.0, b64_buf);
                    }
                } else {
                    ESP_LOGW(TAG, "👻 No face detected.");
                    broadcast_ws_payload("NO_FACE", -1, 0.0, b64_buf);
                }

                free(rgb888_buf);
                if (b64_buf) free(b64_buf);
                esp_camera_fb_return(fb);
                ESP_LOGI(TAG, "👉 Ready.");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

#ifdef __cplusplus
}
#endif