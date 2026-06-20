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
    config.grab_mode = CAMERA_GRAB_LATEST; 

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }

    sensor_t * s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, 1);   
        s->set_hmirror(s, 1); 
    }
    ESP_LOGI(TAG, "Camera ready.");
}

void init_gpio() {
    // ========================================================
    // OPEN DRAIN LOGIC: Fixes the 3.3V vs 5V Relay Glitch
    // ========================================================
    gpio_config_t relay_conf = {};
    relay_conf.intr_type = GPIO_INTR_DISABLE;
    relay_conf.mode = GPIO_MODE_OUTPUT_OD; // OPEN DRAIN IS THE KEY
    relay_conf.pin_bit_mask = (1ULL << RELAY_PIN);
    relay_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    relay_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&relay_conf);

    // Initial State: 1 = High Impedance (Relay internal pullup to 5V triggers -> Relay OFF)
    gpio_set_level(RELAY_PIN, 1); 

    gpio_set_direction(FLASH_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASH_PIN, 0); 

    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY); 
}

void button_polling_task(void *pvParameters) {
    bool last_state = true; 
    while(1) {
        bool current_state = gpio_get_level(BUTTON_PIN);
        if (current_state == 0 && last_state == 1) {
            if (capture_task_handle != NULL) xTaskNotifyGive(capture_task_handle);
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
        
        // 0 = Pulled to GND -> Relay turns ON
        gpio_set_level(RELAY_PIN, 0); 
        vTaskDelay(pdMS_TO_TICKS(5000)); 
        
        ESP_LOGI(TAG, "LOCKING DOOR: Deactivating Relay.");
        
        // 1 = High Impedance -> Relay internal pullup takes over -> Relay OFF
        gpio_set_level(RELAY_PIN, 1); 
    }
}

void capture_task(void *pvParameters) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!is_ws_connected) continue;

        gpio_set_level(FLASH_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(150)); 
        
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);

        fb = esp_camera_fb_get();
        gpio_set_level(FLASH_PIN, 0);

        if (!fb) continue;
        
        ESP_LOGI(TAG, "Image captured!");
        esp_websocket_client_send_bin(ws_client, (const char *)fb->buf, fb->len, portMAX_DELAY);
        esp_camera_fb_return(fb); 
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_wifi_connected = false; 
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) is_wifi_connected = true;
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            is_ws_connected = true; 
            ESP_LOGI(TAG, "Websocket connected");
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            is_ws_connected = false; 
            break;
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x01) { 
                char* response = (char*)malloc(data->data_len + 1);
                memcpy(response, data->data_ptr, data->data_len);
                response[data->data_len] = '\0';
                
                if (strstr(response, "UNLOCK") != NULL) {
                    if (relay_task_handle != NULL) xTaskNotifyGive(relay_task_handle);
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

extern "C" void app_main() {
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