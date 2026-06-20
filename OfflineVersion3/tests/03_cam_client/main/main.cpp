#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_websocket_client.h"
#include "esp_timer.h"
#include "mbedtls/base64.h" // NEW: Added for Base64 Encoding

static const char *TAG = "CAM_CLIENT";

// ========================================================
// 1. HARDWARE DEFINITIONS (AI-Thinker Board)
// ========================================================
#define BUTTON_PIN GPIO_NUM_12
#define RELAY_PIN  GPIO_NUM_13
#define FLASH_PIN  GPIO_NUM_4  

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

esp_websocket_client_handle_t ws_client = NULL;
TaskHandle_t capture_task_handle = NULL;
TaskHandle_t relay_task_handle = NULL;

bool is_wifi_connected = false;
bool is_ws_connected = false;

// ========================================================
// NEW HELPER: Base64 Image Printer
// ========================================================
void print_base64_image(const uint8_t *image_buf, size_t image_len) {
    size_t output_len;
    // Calculate required size for Base64 string
    mbedtls_base64_encode(NULL, 0, &output_len, image_buf, image_len);
    
    unsigned char *base64_buf = (unsigned char *)malloc(output_len + 1);
    if (base64_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for Base64 encoding");
        return;
    }
    
    int ret = mbedtls_base64_encode(base64_buf, output_len, &output_len, image_buf, image_len);
    if (ret == 0) {
        base64_buf[output_len] = '\0';
        ESP_LOGI(TAG, "--- BASE64 IMAGE START (Copy the line below to browser URL bar) ---");
        
        // Print prefix
        printf("data:image/jpeg;base64,");
        
        // Print in 1024-byte chunks to prevent Windows CMD/Terminal truncation limits
        for (size_t i = 0; i < output_len; i += 1024) {
            printf("%.*s", (int)(output_len - i > 1024 ? 1024 : output_len - i), base64_buf + i);
        }
        printf("\n");
        
        ESP_LOGI(TAG, "--- BASE64 IMAGE END ---");
    } else {
        ESP_LOGE(TAG, "Base64 encode failed: %d", ret);
    }
    free(base64_buf);
}

// ========================================================
// 3. HARDWARE INITIALIZATION
// ========================================================
void init_camera() {
    camera_config_t config = {};
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
    
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA; 
    config.jpeg_quality = 12;           
    config.fb_count = 2;       
    config.grab_mode = CAMERA_GRAB_LATEST; // THE FIX: Always keep the most recent frame!         

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }

    // --- NEW: Flip camera 180 degrees at the hardware level ---
    sensor_t * s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, 1);   // Flip Vertical
        s->set_hmirror(s, 1); // Mirror Horizontal
        ESP_LOGI(TAG, "Hardware orientation adjusted: Flipped 180 degrees.");
    }

    ESP_LOGI(TAG, "Camera initialized successfully.");
}

void init_gpio() {
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_PIN, 0); 

    gpio_set_direction(FLASH_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASH_PIN, 0); 

    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY); 
}

// ========================================================
// 4. THE TASKS (POLLING, RELAY, & CAPTURE)
// ========================================================
void button_polling_task(void *pvParameters) {
    bool last_state = true; 
    while(1) {
        bool current_state = gpio_get_level(BUTTON_PIN);
        if (current_state == 0 && last_state == 1) {
            ESP_LOGI(TAG, "=> Button Hardware Press Detected! (Pulled to GND)");
            if (capture_task_handle != NULL) {
                xTaskNotifyGive(capture_task_handle);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void relay_task(void *pvParameters) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        ESP_LOGI(TAG, "===================================");
        ESP_LOGI(TAG, "UNLOCKING DOOR: Activating Relay...");
        ESP_LOGI(TAG, "===================================");
        
        gpio_set_level(RELAY_PIN, 1); 
        vTaskDelay(pdMS_TO_TICKS(5000)); 
        
        ESP_LOGI(TAG, "LOCKING DOOR: Deactivating Relay.");
        gpio_set_level(RELAY_PIN, 0);
    }
}

void capture_task(void *pvParameters) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!is_ws_connected) {
            ESP_LOGW(TAG, "WebSocket disconnected. Cannot send frame.");
            continue;
        }

        ESP_LOGI(TAG, "[1] Waking Camera & Activating Flash...");
        gpio_set_level(FLASH_PIN, 1);
        
        // Wait for the flash to fully illuminate the subject
        vTaskDelay(pdMS_TO_TICKS(150)); 
        
        // FLUSH THE BUFFER: Grab one frame and immediately throw it away.
        // This forces the camera to discard the frame taken while it was dark,
        // and allows the Auto-Exposure to adjust to the bright Flash LED.
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);

        int64_t start = esp_timer_get_time();
        
        // GRAB THE REAL FRAME: This one will be instantaneous and perfectly lit.
        fb = esp_camera_fb_get();
        
        // Instantly turn off the Flash LED to save power and eyes
        gpio_set_level(FLASH_PIN, 0);

        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed! Is the ribbon loose?");
            continue;
        }
        
        ESP_LOGI(TAG, "[2] Captured %d bytes of JPEG data. Transmitting to S3 Brain...", fb->len);
        esp_websocket_client_send_bin(ws_client, (const char *)fb->buf, fb->len, portMAX_DELAY);
        
        int64_t end = esp_timer_get_time();
        ESP_LOGI(TAG, "[3] Frame transmission complete. Took %lld ms.", (end - start) / 1000);
        
        // Print Base64 to console
        print_base64_image(fb->buf, fb->len);

        esp_camera_fb_return(fb); 
        
        ESP_LOGI(TAG, "Waiting for S3 Brain to reply...");
    }
}

// ========================================================
// 5. NETWORKING (WIFI & WEBSOCKET)
// ========================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_wifi_connected = false;
        ESP_LOGW(TAG, "WiFi Disconnected. Reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        is_wifi_connected = true;
        ESP_LOGI(TAG, "WiFi Connected to S3 Brain!");
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            is_ws_connected = true;
            ESP_LOGI(TAG, "WebSocket Connected to ws://192.168.4.1/ws");
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            is_ws_connected = false;
            ESP_LOGW(TAG, "WebSocket Disconnected");
            break;
            
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x01) { 
                char* response = (char*)malloc(data->data_len + 1);
                memcpy(response, data->data_ptr, data->data_len);
                response[data->data_len] = '\0';
                
                ESP_LOGI(TAG, ">>> S3 BRAIN VERDICT: %s <<<", response);
                
                if (strstr(response, "MATCH:") != NULL) {
                    if (relay_task_handle != NULL) {
                        xTaskNotifyGive(relay_task_handle);
                    }
                } else if (strstr(response, "NO MATCH") != NULL) {
                    ESP_LOGW(TAG, "Stranger detected. Relay remains locked.");
                }
                
                free(response);
            }
            break;
    }
}

void init_wifi_and_ws() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, "ESP32_ML_BRAIN");
    strcpy((char*)wifi_config.sta.password, "12345678");

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = "ws://192.168.4.1/ws";
    ws_cfg.buffer_size = 32768; 
    ws_cfg.reconnect_timeout_ms = 10000; 
    ws_cfg.network_timeout_ms = 10000;
    
    ws_client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)ws_client);
    esp_websocket_client_start(ws_client);
}

// ========================================================
// 6. MAIN ENTRY POINT
// ========================================================
extern "C" void app_main() {
    ESP_LOGI(TAG, "--- Booting ESP32-CAM Client ---");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    init_gpio();
    init_camera();
    vTaskDelay(pdMS_TO_TICKS(500)); 
    init_wifi_and_ws();

    xTaskCreatePinnedToCore(capture_task, "capture_task", 1024 * 6, NULL, 10, &capture_task_handle, 1);
    xTaskCreatePinnedToCore(relay_task, "relay_task", 1024 * 4, NULL, 5, &relay_task_handle, 1);
    xTaskCreatePinnedToCore(button_polling_task, "button_task", 1024 * 4, NULL, 10, NULL, 1);
}