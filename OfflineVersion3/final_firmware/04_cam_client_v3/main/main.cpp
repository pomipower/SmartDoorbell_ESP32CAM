#include <stdio.h>
#include <string.h>
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

static const char *TAG = "CAM_CLIENT";

#define BUTTON_PIN GPIO_NUM_14
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

esp_websocket_client_handle_t ws_client;
bool is_ws_connected = false;
bool is_live_streaming = false; // LIVE VIDEO STATE FLAG
volatile bool got_ip = false;

void init_camera() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAM_PIN_Y2;
    config.pin_d1 = CAM_PIN_Y3;
    config.pin_d2 = CAM_PIN_Y4;
    config.pin_d3 = CAM_PIN_Y5;
    config.pin_d4 = CAM_PIN_Y6;
    config.pin_d5 = CAM_PIN_Y7;
    config.pin_d6 = CAM_PIN_Y8;
    config.pin_d7 = CAM_PIN_Y9;
    config.pin_xclk = CAM_PIN_XCLK;
    config.pin_pclk = CAM_PIN_PCLK;
    config.pin_vsync = CAM_PIN_VSYNC;
    config.pin_href = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn = CAM_PIN_PWDN;
    config.pin_reset = CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000;
    
    // QVGA is best for ML model input speed
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }
    ESP_LOGI(TAG, "Camera initialized successfully");
}

void relay_task(void *arg) {
    gpio_set_level(RELAY_PIN, 0); 
    vTaskDelay(pdMS_TO_TICKS(5000));
    gpio_set_level(RELAY_PIN, 1); 
    ESP_LOGI(TAG, "LOCKING DOOR: Deactivating Relay.");
    vTaskDelete(NULL); // Kills the task when done
}

void take_picture_and_send() {
    if (!is_ws_connected) {
        ESP_LOGE(TAG, "WebSocket not connected!");
        return;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    
    fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);

    // Now capture the 3rd, 100% real-time frame
    fb = esp_camera_fb_get(); 
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return;
    }

    gpio_set_level(FLASH_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(FLASH_PIN, 0);

    ESP_LOGI(TAG, "Image captured! Size: %d bytes. Sending...", fb->len);
    esp_websocket_client_send_bin(ws_client, (const char *)fb->buf, fb->len, portMAX_DELAY);
    esp_camera_fb_return(fb);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Websocket connected");
        is_ws_connected = true;
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Websocket disconnected");
        is_ws_connected = false;
        is_live_streaming = false; // Failsafe
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01) { 
            if (strncmp((char *)data->data_ptr, "CMD:UNLOCK", 10) == 0) {
                ESP_LOGI(TAG, "===================================");
                ESP_LOGI(TAG, "UNLOCKING DOOR: Activating Relay...");
                ESP_LOGI(TAG, "===================================");
                
                // FIX: Spawns an independent task so the network stack doesn't freeze!
                xTaskCreate(relay_task, "relay_task", 2048, NULL, 5, NULL);
            }
            else if (strncmp((char *)data->data_ptr, "CMD:START_STREAM", 16) == 0) {
                ESP_LOGI(TAG, "Starting Live Video Stream (~10 FPS)");
                is_live_streaming = true;
            }
            else if (strncmp((char *)data->data_ptr, "CMD:STOP_STREAM", 15) == 0) {
                ESP_LOGI(TAG, "Stopping Live Video Stream");
                is_live_streaming = false;
            }
        }
        break;
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Reconnecting to AP...");
        got_ip = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        got_ip = true;
    }
}

// --------------------------------------------------------
// BACKGROUND VIDEO STREAM TASK (10 FPS)
// --------------------------------------------------------
void video_stream_task(void *pvParameters) {
    while (1) {
        if (is_live_streaming && is_ws_connected) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb) {
                esp_websocket_client_send_bin(ws_client, (const char *)fb->buf, fb->len, portMAX_DELAY);
                esp_camera_fb_return(fb);
            }
            vTaskDelay(pdMS_TO_TICKS(40));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void button_task(void *pvParameters) {
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

    while(1) {
        if (gpio_get_level(BUTTON_PIN) == 0) { 
            // DISABLE PHYSICAL BUTTON INTERACTION DURING VIDEO CALL
            if (is_live_streaming) {
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            
            ESP_LOGI(TAG, "Doorbell Pressed! Taking picture...");
            take_picture_and_send();
            vTaskDelay(pdMS_TO_TICKS(3000)); // Anti-spam delay
        }
        vTaskDelay(pdMS_TO_TICKS(50));
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

    ESP_LOGI(TAG, "Waiting for IP address from S3 Hub...");
    while (!got_ip) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "Network is up! Starting WebSocket...");

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = "ws://192.168.4.1/ws";
    ws_cfg.buffer_size = 102400; // BUFFED: Prevents binary frame fragmentation
    ws_cfg.reconnect_timeout_ms = 10000; 
    ws_cfg.network_timeout_ms = 10000;
    
    ws_client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)ws_client);
    esp_websocket_client_start(ws_client);
}

extern "C" void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    gpio_reset_pin(RELAY_PIN);
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT_OD); // FIX: Open-Drain floats the 3.3V pin
    gpio_set_level(RELAY_PIN, 1);

    gpio_reset_pin(FLASH_PIN);
    gpio_set_direction(FLASH_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASH_PIN, 0);

    init_camera();
    init_wifi_and_ws();

    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);
    xTaskCreate(video_stream_task, "video_stream", 4096, NULL, 5, NULL); // SPAWNS THE STREAM ENGINE
}