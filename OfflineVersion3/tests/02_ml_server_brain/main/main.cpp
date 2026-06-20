#include <stdio.h>
#include <string.h>
#include <vector>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

// Bulletproof JPEG Decoder from esp32-camera
#include "img_converters.h"

// ESP-DL Vision Models (Global Namespace in v1.1.0)
#include "human_face_detect_msr01.hpp"
#include "face_recognition_112_v1_s8.hpp"

static const char *TAG = "S3_ML_BRAIN";

// ========================================================
// 1. SPIFFS DATABASE DEFINITIONS
// ========================================================
struct UserEmbedding {
    char name[32];
    float feature[128];
};

std::vector<UserEmbedding> face_db;
bool enroll_next_image = false;
char pending_enroll_name[32] = "";

void init_spiffs() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/fr",
        .partition_label = "fr",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_vfs_spiffs_register(&conf);
    
    FILE* f = fopen("/fr/faces.db", "rb");
    if (f) {
        UserEmbedding temp;
        while (fread(&temp, sizeof(UserEmbedding), 1, f) == 1) {
            face_db.push_back(temp);
            ESP_LOGI(TAG, "Loaded DB Entry: %s", temp.name);
        }
        fclose(f);
    }
}

void save_to_spiffs(const char* name, float* embedding) {
    UserEmbedding new_user;
    strncpy(new_user.name, name, 31);
    memcpy(new_user.feature, embedding, sizeof(float) * 128);
    
    face_db.push_back(new_user);

    FILE* f = fopen("/fr/faces.db", "ab");
    if (f) {
        fwrite(&new_user, sizeof(UserEmbedding), 1, f);
        fclose(f);
        ESP_LOGI(TAG, "Successfully saved %s to NVS/SPIFFS.", name);
    }
}

// ========================================================
// 2. CUSTOM ML UTILS
// ========================================================
float calculate_cosine_similarity(float* a, float* b, int len) {
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for(int i = 0; i < len; i++) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    if(normA == 0.0f || normB == 0.0f) return 0.0f;
    return dot / (sqrt(normA) * sqrt(normB));
}

void align_crop_face_nn(uint8_t* src, int src_w, int src_h, const std::vector<int>& box, uint8_t* dest, int dest_w, int dest_h) {
    int x1 = box[0], y1 = box[1], x2 = box[2], y2 = box[3];
    
    if(x1 < 0) x1 = 0; 
    if(y1 < 0) y1 = 0;
    if(x2 >= src_w) x2 = src_w - 1; 
    if(y2 >= src_h) y2 = src_h - 1;
    
    int crop_w = x2 - x1 + 1;
    int crop_h = y2 - y1 + 1;
    if(crop_w <= 0 || crop_h <= 0) return;
    
    for (int y = 0; y < dest_h; y++) {
        for (int x = 0; x < dest_w; x++) {
            int src_x = x1 + (x * crop_w) / dest_w;
            int src_y = y1 + (y * crop_h) / dest_h;
            
            if (src_x >= src_w) src_x = src_w - 1;
            if (src_y >= src_h) src_y = src_h - 1;

            int dest_idx = (y * dest_w + x) * 3;
            int src_idx = (src_y * src_w + src_x) * 3;

            dest[dest_idx]     = src[src_idx];
            dest[dest_idx + 1] = src[src_idx + 1];
            dest[dest_idx + 2] = src[src_idx + 2];
        }
    }
}

// ========================================================
// 3. ML QUEUE & TASK (CORE 1)
// ========================================================
struct MLJob {
    uint8_t* jpeg_buf;
    size_t jpeg_len;
    int client_fd;
};

QueueHandle_t ml_queue;
httpd_handle_t global_server = NULL;

void ml_inference_task(void *pvParameters) {
    ESP_LOGI(TAG, "ML Core Online. Initializing esp-dl Models...");
    
    HumanFaceDetectMSR01 detector(0.3F, 0.5F, 10, 1.0F); 
    FaceRecognition112V1S8 recognizer;

    MLJob job;
    while (1) {
        if (xQueueReceive(ml_queue, &job, portMAX_DELAY)) {
            int64_t start_time = esp_timer_get_time();

            int img_w = 320; 
            int img_h = 240; 
            uint8_t *rgb_buf = (uint8_t *)heap_caps_malloc(img_w * img_h * 3, MALLOC_CAP_SPIRAM);
            
            if (!rgb_buf) {
                ESP_LOGE(TAG, "Failed to alloc PSRAM for RGB buffer");
                free(job.jpeg_buf);
                continue;
            }

            bool dec_res = fmt2rgb888(job.jpeg_buf, job.jpeg_len, PIXFORMAT_JPEG, rgb_buf);
            free(job.jpeg_buf); 
            
            if (!dec_res) {
                ESP_LOGE(TAG, "JPEG Decode Failed.");
                free(rgb_buf);
                continue;
            }

            std::list<dl::detect::result_t> &detect_results = detector.infer(rgb_buf, {img_h, img_w, 3});

            if (detect_results.size() > 0) {
                uint8_t* aligned_face = (uint8_t*)heap_caps_malloc(112 * 112 * 3, MALLOC_CAP_SPIRAM);
                if (!aligned_face) {
                    free(rgb_buf);
                    continue;
                }
                
                align_crop_face_nn(rgb_buf, img_w, img_h, detect_results.front().box, aligned_face, 112, 112);

                int8_t* input_int8 = (int8_t*)heap_caps_malloc(112 * 112 * 3, MALLOC_CAP_SPIRAM);
                for(int i = 0; i < 112 * 112 * 3; i++) {
                    input_int8[i] = (int8_t)((int)aligned_face[i] - 128);
                }

                dl::Tensor<int8_t> input_tensor;
                input_tensor.set_element(input_int8).set_shape({112, 112, 3}).set_auto_free(false);
                
                auto& feature = recognizer.forward(input_tensor);
                int8_t* feature_ptr_int8 = (int8_t*)feature.get_element_ptr();

                float feature_ptr[128];
                for(int i = 0; i < 128; i++) {
                    feature_ptr[i] = (float)feature_ptr_int8[i];
                }

                char response_msg[64] = "NO MATCH";
                
                if (enroll_next_image) {
                    save_to_spiffs(pending_enroll_name, feature_ptr);
                    snprintf(response_msg, sizeof(response_msg), "ENROLLED: %s", pending_enroll_name);
                    enroll_next_image = false;
                } else {
                    float highest_similarity = 0.0f;
                    char best_match[32] = "";
                    for (const auto& user : face_db) {
                        float similarity = calculate_cosine_similarity(feature_ptr, (float*)user.feature, 128);
                        if (similarity > highest_similarity) {
                            highest_similarity = similarity;
                            strcpy(best_match, user.name);
                        }
                    }
                    if (highest_similarity > 0.6f) { 
                        snprintf(response_msg, sizeof(response_msg), "MATCH: %s (%.2f)", best_match, highest_similarity);
                    }
                }

                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t*)response_msg;
                ws_pkt.len = strlen(response_msg);
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;
                httpd_ws_send_frame_async(global_server, job.client_fd, &ws_pkt);

                int64_t end_time = esp_timer_get_time();
                ESP_LOGI(TAG, "Pipeline Finished in %lld ms. Result: %s", (end_time - start_time)/1000, response_msg);

                free(aligned_face);
                free(input_int8);
            } else {
                ESP_LOGW(TAG, "No face detected in image.");

                char fail_msg[] = "NO FACE DETECTED";
                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t*)fail_msg;
                ws_pkt.len = strlen(fail_msg);
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;
                httpd_ws_send_frame_async(global_server, job.client_fd, &ws_pkt);
            }

            free(rgb_buf);
        }
    }
}

// ========================================================
// 4. WEBSOCKET SERVER (CORE 0)
// ========================================================
esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK || ws_pkt.len == 0) return ret;

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        
        char* text = (char*)ws_pkt.payload;
        if (strncmp(text, "ENROLL:", 7) == 0) {
            strncpy(pending_enroll_name, text + 7, 31);
            enroll_next_image = true;
            ESP_LOGI(TAG, "Enrollment mode armed for: %s", pending_enroll_name);
        }
        free(buf);
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
        uint8_t* jpeg_buf = (uint8_t*)heap_caps_malloc(ws_pkt.len, MALLOC_CAP_SPIRAM);
        ws_pkt.payload = jpeg_buf;
        httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

        MLJob job;
        job.jpeg_buf = jpeg_buf;
        job.jpeg_len = ws_pkt.len;
        job.client_fd = httpd_req_to_sockfd(req);
        
        if (xQueueSend(ml_queue, &job, 0) != pdTRUE) {
            ESP_LOGE(TAG, "ML Queue Full! Dropping frame.");
            free(jpeg_buf);
        }
    }
    return ESP_OK;
}

httpd_handle_t start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 5;
    
    httpd_uri_t ws_uri = {}; 
    ws_uri.uri        = "/ws";
    ws_uri.method     = HTTP_GET;
    ws_uri.handler    = ws_handler;
    ws_uri.user_ctx   = NULL;
    ws_uri.is_websocket = true;

    if (httpd_start(&global_server, &config) == ESP_OK) {
        httpd_register_uri_handler(global_server, &ws_uri);
        return global_server;
    }
    return NULL;
}

// ========================================================
// 5. WIFI SOFT-AP SETUP
// ========================================================
void wifi_init_softap() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, "ESP32_ML_BRAIN");
    strcpy((char*)wifi_config.ap.password, "12345678");
    wifi_config.ap.ssid_len = strlen("ESP32_ML_BRAIN");
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
    
    ESP_LOGI(TAG, "SoftAP Started. SSID: ESP32_ML_BRAIN");
}

// ========================================================
// 6. MAIN ENTRY POINT
// ========================================================
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "--- Booting Distributed Edge-AI Server ---");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    init_spiffs();
    wifi_init_softap();

    ml_queue = xQueueCreate(3, sizeof(MLJob));

    xTaskCreatePinnedToCore(ml_inference_task, "ML_Task", 1024 * 32, NULL, 5, NULL, 1);
    start_webserver();
}