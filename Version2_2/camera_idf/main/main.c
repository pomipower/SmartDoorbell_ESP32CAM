// ==============================================================================
//  SMART DOORBELL — OUTDOOR UNIT (THERMALLY STABLE 10FPS)
// ==============================================================================

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_websocket_client.h"
#include <esp_http_server.h>

// ── User Configuration ────────────────────────────────────────────────────────
#define WIFI_SSID           "Galaxy S24"
#define WIFI_PASS           "qwne3344"
#define WS_SERVER_URI       "ws://172.28.199.250:5050/ws/camera"

#define BUTTON_PIN          1
#define RELAY_PIN           13
#define STATUS_LED_PIN      4

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_Y9 35
#define CAM_PIN_Y8 34
#define CAM_PIN_Y7 39
#define CAM_PIN_Y6 36
#define CAM_PIN_Y5 21
#define CAM_PIN_Y4 19
#define CAM_PIN_Y3 18
#define CAM_PIN_Y2 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

esp_websocket_client_handle_t ws_client;
bool is_ws_connected = false;
volatile bool is_wifi_connected = false;
char my_ip_str[16] = ""; 

void unlock_door() {
    gpio_set_level(RELAY_PIN, 0); 
    gpio_set_level(STATUS_LED_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(3000));
    gpio_set_level(RELAY_PIN, 1); 
    gpio_set_level(STATUS_LED_PIN, 0);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        printf("✅ WebSocket Connected to Server!\n");
        is_ws_connected = true;
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
        printf("❌ WebSocket Disconnected.\n");
        is_ws_connected = false;
    } else if (event_id == WEBSOCKET_EVENT_DATA) {
        if (data->op_code == 0x01 && data->data_len > 0) {
            if (strncmp((char *)data->data_ptr, "UNLOCK", data->data_len) == 0) {
                printf("🔓 Received UNLOCK command from server!\n");
                unlock_door();
            }
        }
    }
}

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK) return res;

    printf("🎥 FastAPI Server requested Video Stream!\n");

    while(true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            printf("⚠️ Camera capture failed\n");
            res = ESP_FAIL;
        } else {
            size_t hlen = snprintf(part_buf, 64, _STREAM_PART, fb->len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
            if(res == ESP_OK) {
                res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
            }
            if(res == ESP_OK) {
                res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
            }
            esp_camera_fb_return(fb); 
        }
        
        if(res != ESP_OK) {
            printf("⚠️ Stream send failed. Breaking loop.\n");
            break;
        }
        
        // CRITICAL FIX: Give the Wi-Fi MAC hardware time to breathe and cool down!
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
    printf("❌ Stream Connection Closed.\n");
    return res;
}

void start_camera_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true; 
    config.send_wait_timeout = 5; 
    config.recv_wait_timeout = 5;
    
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);
        printf("✅ HTTP MJPEG Server Started on port 81\n");
    }
}

void button_task(void *pvParameters) {
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) {
            printf("🔔 Button Pressed!\n");
            gpio_set_level(STATUS_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(STATUS_LED_PIN, 0);
            while (gpio_get_level(BUTTON_PIN) == 0) { vTaskDelay(pdMS_TO_TICKS(10)); }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_STA_START || event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(my_ip_str, sizeof(my_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        printf("✅ Wi-Fi Connected! IP: %s\n", my_ip_str);
        is_wifi_connected = true;
    }
}

void init_wifi() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    wifi_config_t wifi_config = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS, }, };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

void init_camera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0 = CAM_PIN_Y2; config.pin_d1 = CAM_PIN_Y3; config.pin_d2 = CAM_PIN_Y4;
    config.pin_d3 = CAM_PIN_Y5; config.pin_d4 = CAM_PIN_Y6; config.pin_d5 = CAM_PIN_Y7;
    config.pin_d6 = CAM_PIN_Y8; config.pin_d7 = CAM_PIN_Y9; config.pin_xclk = CAM_PIN_XCLK;
    config.pin_pclk = CAM_PIN_PCLK; config.pin_vsync = CAM_PIN_VSYNC; config.pin_href = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD; config.pin_sccb_scl = CAM_PIN_SIOC; config.pin_pwdn = CAM_PIN_PWDN;
    config.pin_reset = CAM_PIN_RESET; 
    
    // CRITICAL FIX: Underclock the camera to 10MHz. 
    // This dramatically reduces CPU load and heat while easily supplying 10 FPS.
    config.xclk_freq_hz = 10000000; 
    
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA; 
    config.jpeg_quality = 12; 
    config.fb_count = 2; 
    config.fb_location = CAMERA_FB_IN_PSRAM;
    esp_camera_init(&config);
}

void app_main(void) {
    nvs_flash_init();
    gpio_reset_pin(RELAY_PIN); gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT); gpio_set_level(RELAY_PIN, 1);
    gpio_reset_pin(STATUS_LED_PIN); gpio_set_direction(STATUS_LED_PIN, GPIO_MODE_OUTPUT);

    init_wifi();
    printf("⏳ Waiting for IP address...\n");
    while (!is_wifi_connected) { vTaskDelay(pdMS_TO_TICKS(100)); }
    
    init_camera();
    start_camera_server();

    esp_websocket_client_config_t ws_cfg = {
        .uri = WS_SERVER_URI,
        .network_timeout_ms = 10000,
        .reconnect_timeout_ms = 10000,
        .buffer_size = 1024, 
    };
    ws_client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)ws_client);
    esp_websocket_client_start(ws_client);

    while (!is_ws_connected) { vTaskDelay(pdMS_TO_TICKS(100)); }

    char ip_json[64];
    snprintf(ip_json, sizeof(ip_json), "{\"ip\": \"%s\"}", my_ip_str);
    esp_websocket_client_send_text(ws_client, ip_json, strlen(ip_json), portMAX_DELAY);

    xTaskCreate(button_task, "button_poll", 2048, NULL, 4, NULL);
    
    while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); } 
}