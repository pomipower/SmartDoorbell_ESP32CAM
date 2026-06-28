#include <stdio.h>
#include <string.h>
#include <vector>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "human_face_detect_msr01.hpp"
#include "face_recognition_112_v1_s8.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"

// Graphics & UI
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include "lvgl.h"

static const char *TAG = "S3_BRAIN_HUB";

// ========================================================
// 1. HARDWARE DISPLAY OVERRIDES (PANLEE BC02)
// ========================================================
spi_device_handle_t g_screen_spi;

static void write_cmd(uint16_t cmd) {
    spi_transaction_ext_t trans = {};
    trans.base.flags = SPI_TRANS_VARIABLE_CMD; trans.base.cmd = (cmd | 0x0000);
    trans.command_bits = 9;
    ESP_ERROR_CHECK(spi_device_transmit(g_screen_spi, (spi_transaction_t*)&trans));
}
static void write_data(uint16_t data) {
    spi_transaction_ext_t trans = {};
    trans.base.flags = SPI_TRANS_VARIABLE_CMD; trans.base.cmd = (data | 0x0100);
    trans.command_bits = 9;
    ESP_ERROR_CHECK(spi_device_transmit(g_screen_spi, (spi_transaction_t*)&trans));
}

static void panelLan_rgb_spi_init() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = 48; buscfg.miso_io_num = -1; buscfg.sclk_io_num = 45; 
    buscfg.quadwp_io_num = -1; buscfg.quadhd_io_num = -1; buscfg.max_transfer_sz = 10 * 1024;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0; devcfg.clock_speed_hz = 10 * 1000 * 1000;
    devcfg.spics_io_num = 38; devcfg.queue_size = 7;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &g_screen_spi));

    write_cmd(0xF0); write_data(0x55); write_data(0xAA); write_data(0x52); write_data(0x08); write_data(0x00);
    write_cmd(0xF6); write_data(0x5A); write_data(0x87);
    write_cmd(0xC1); write_data(0x3F); write_cmd(0xC2); write_data(0x0E);
    write_cmd(0xC6); write_data(0xF8); write_cmd(0xC9); write_data(0x10);
    write_cmd(0xCD); write_data(0x25); write_cmd(0xF8); write_data(0x8A);
    write_cmd(0xAC); write_data(0x45); write_cmd(0xA0); write_data(0xDD);
    write_cmd(0xA7); write_data(0x47);
    write_cmd(0xFA); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04);
    write_cmd(0x86); write_data(0x99); write_data(0xa3); write_data(0xa3); write_data(0x51);
    write_cmd(0xA3); write_data(0xEE);
    write_cmd(0xFD); write_data(0x28); write_data(0x28); write_data(0x00);
    write_cmd(0x71); write_data(0x48); write_cmd(0x72); write_data(0x48);
    write_cmd(0x73); write_data(0x00); write_data(0x44);
    write_cmd(0x97); write_data(0xEE); write_cmd(0x83); write_data(0x93);
    write_cmd(0x9A); write_data(0x72); write_cmd(0x9B); write_data(0x5a);
    write_cmd(0x82); write_data(0x2c); write_data(0x2c); write_cmd(0xB1); write_data(0x10);
    write_cmd(0x6D); write_data(0x00); write_data(0x1F); write_data(0x19); write_data(0x1A); write_data(0x10); write_data(0x0e); write_data(0x0c); write_data(0x0a); write_data(0x02); write_data(0x07); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x08); write_data(0x01); write_data(0x09); write_data(0x0b); write_data(0x0D); write_data(0x0F); write_data(0x1a); write_data(0x19); write_data(0x1f); write_data(0x00);
    write_cmd(0x64); write_data(0x38); write_data(0x05); write_data(0x01); write_data(0xdb); write_data(0x03); write_data(0x03); write_data(0x38); write_data(0x04); write_data(0x01); write_data(0xdc); write_data(0x03); write_data(0x03); write_data(0x7A); write_data(0x7A); write_data(0x7A); write_data(0x7A);
    write_cmd(0x65); write_data(0x38); write_data(0x03); write_data(0x01); write_data(0xdd); write_data(0x03); write_data(0x03); write_data(0x38); write_data(0x02); write_data(0x01); write_data(0xde); write_data(0x03); write_data(0x03); write_data(0x7A); write_data(0x7A); write_data(0x7A); write_data(0x7A);
    write_cmd(0x66); write_data(0x38); write_data(0x01); write_data(0x01); write_data(0xdf); write_data(0x03); write_data(0x03); write_data(0x38); write_data(0x00); write_data(0x01); write_data(0xe0); write_data(0x03); write_data(0x03); write_data(0x7A); write_data(0x7A); write_data(0x7A); write_data(0x7A);
    write_cmd(0x67); write_data(0x30); write_data(0x01); write_data(0x01); write_data(0xe1); write_data(0x03); write_data(0x03); write_data(0x30); write_data(0x02); write_data(0x01); write_data(0xe2); write_data(0x03); write_data(0x03); write_data(0x7A); write_data(0x7A); write_data(0x7A); write_data(0x7A);
    write_cmd(0x68); write_data(0x00); write_data(0x08); write_data(0x15); write_data(0x08); write_data(0x15); write_data(0x7A); write_data(0x7A); write_data(0x08); write_data(0x15); write_data(0x08); write_data(0x15); write_data(0x7A); write_data(0x7A);
    write_cmd(0x60); write_data(0x38); write_data(0x08); write_data(0x7A); write_data(0x7A); write_data(0x38); write_data(0x09); write_data(0x7A); write_data(0x7A);
    write_cmd(0x63); write_data(0x31); write_data(0xe4); write_data(0x7A); write_data(0x7A); write_data(0x31); write_data(0xe5); write_data(0x7A); write_data(0x7A);
    write_cmd(0x69); write_data(0x04); write_data(0x22); write_data(0x14); write_data(0x22); write_data(0x14); write_data(0x22); write_data(0x08);
    write_cmd(0x6B); write_data(0x07); write_cmd(0x7A); write_data(0x08); write_data(0x13); write_cmd(0x7B); write_data(0x08); write_data(0x13);
    write_cmd(0xD1); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD2); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD3); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD4); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD5); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD6); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0x3A); write_data(0x66);

    write_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120)); write_cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));
    spi_bus_remove_device(g_screen_spi); spi_bus_free(SPI2_HOST);
}

class Panel_BC02_Spec : public lgfx::Panel_RGB {
public:
    bool init(bool use_reset) override {
        gpio_reset_pin((gpio_num_t)39); gpio_reset_pin((gpio_num_t)40);
        gpio_reset_pin((gpio_num_t)41); gpio_reset_pin((gpio_num_t)42);
        gpio_set_direction((gpio_num_t)41, GPIO_MODE_OUTPUT); gpio_set_level((gpio_num_t)41, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        panelLan_rgb_spi_init();
        if (!lgfx::Panel_RGB::init(false)) return false;
        return true;
    }
};

class LGFX : public lgfx::LGFX_Device {
    Panel_BC02_Spec _panel_instance;
    lgfx::Bus_RGB   _bus_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_FT5x06 _touch_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;
            cfg.pin_d0 = 45; cfg.pin_d1 = 48; cfg.pin_d2 = 47; cfg.pin_d3 = 0;
            cfg.pin_d4 = 21; cfg.pin_d5 = 14; cfg.pin_d6 = 13; cfg.pin_d7 = 12;
            cfg.pin_d8 = 11; cfg.pin_d9 = 16; cfg.pin_d10 = 17; cfg.pin_d11 = 18;
            cfg.pin_d12 = 8; cfg.pin_d13 = 3; cfg.pin_d14 = 46; cfg.pin_d15 = 10;
            cfg.pin_pclk = 39; cfg.pin_vsync = 41; cfg.pin_hsync = 42; cfg.pin_henable = 40;
            cfg.freq_write = 10000000;  
            cfg.hsync_polarity = 1; cfg.hsync_front_porch = 8; cfg.hsync_pulse_width = 10; cfg.hsync_back_porch = 40;
            cfg.vsync_polarity = 1; cfg.vsync_front_porch = 8; cfg.vsync_pulse_width = 10; cfg.vsync_back_porch = 40;
            cfg.pclk_active_neg = 0;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width = 480; cfg.panel_width = 480;
            cfg.memory_height = 480; cfg.panel_height = 480;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.x_min = 0; cfg.x_max = 480;
            cfg.y_min = 0; cfg.y_max = 480;
            cfg.bus_shared = false; 
            cfg.i2c_port = 1;
            cfg.pin_sda = 15; cfg.pin_scl = 6; cfg.pin_int = 4; cfg.pin_rst = -1;
            cfg.freq = 400000;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 5; cfg.invert = false; cfg.freq = 500; cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        setPanel(&_panel_instance);
    }
};

LGFX tft;

// ========================================================
// 2. STATE & DATABASE (ML ENGINE)
// ========================================================
enum AppMode { MODE_SECURITY, MODE_ENROLL };
AppMode current_mode = MODE_SECURITY;
bool is_live_streaming = false; // LIVE VIDEO STATE FLAG

struct UserEmbedding {
    char name[32];
    float feature[128];
};

std::vector<UserEmbedding> face_db;
float pending_feature[128]; 

uint8_t* enroll_jpeg_buf = NULL;
size_t enroll_jpeg_len = 0;

// THE HARDWARE PING-PONG BUFFER (Prevents 10FPS Memory Crashes)
#define PING_PONG_SIZE 102400
uint8_t* pp_buf[3]; // TRIPLE BUFFER
volatile uint8_t pp_write_idx = 0;

QueueHandle_t ml_queue;
struct MLJob {
    uint8_t buf_idx; // Indicates which Ping-Pong buffer holds the frame
    size_t jpeg_len;
};

// ========================================================
// 3. LVGL UI & EVENT HANDLING
// ========================================================
static SemaphoreHandle_t lvgl_mutex;
lv_obj_t * ui_img_cam; 
lv_obj_t * ui_lbl_status;
lv_obj_t * ui_btn_admit;
lv_obj_t * ui_btn_deny;
lv_obj_t * ui_btn_unlock;
lv_obj_t * ui_btn_stream; // NEW LIVE VIDEO BUTTON
lv_img_dsc_t img_dsc;  
LGFX_Sprite img_sprite(&tft); 

httpd_handle_t global_server = NULL;

void broadcast_ws_msg(httpd_ws_type_t type, const uint8_t *data, size_t len) {
    if(!global_server) return;
    
    // EXCLUDE DASHBOARD: Don't choke web interface with 10 FPS binary stream
    if (type == HTTPD_WS_TYPE_BINARY && is_live_streaming) return; 

    size_t max_clients = 10; int client_fds[10];
    if (httpd_get_client_list(global_server, &max_clients, client_fds) == ESP_OK) {
        for (size_t i = 0; i < max_clients; i++) {
            httpd_ws_frame_t ws_pkt = {}; 
            ws_pkt.payload = (uint8_t*)data; 
            ws_pkt.len = len; 
            ws_pkt.type = type;
            httpd_ws_send_frame_async(global_server, client_fds[i], &ws_pkt);
        }
    }
}

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}

static void my_touch_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint16_t touchX, touchY;
    bool touched = tft.getTouch(&touchX, &touchY);
    if (!touched) { data->state = LV_INDEV_STATE_REL; } 
    else { data->state = LV_INDEV_STATE_PR; data->point.x = touchX; data->point.y = touchY; }
}

static void lv_tick_task(void *arg) {
    lv_tick_inc(2); 
}

void trigger_door_unlock() {
    ESP_LOGI(TAG, "SYS:UNLOCK triggered by UI Touch!");
    const char* unlock_msg = "CMD:UNLOCK";
    broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)unlock_msg, strlen(unlock_msg)); 
    
    if(xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
        lv_obj_add_flag(ui_btn_admit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_btn_deny, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_lbl_status, "Door Unlocked Manually");
        lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0x00FF00), 0);
        xSemaphoreGiveRecursive(lvgl_mutex);
    }
}

static void btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        lv_obj_t * btn = lv_event_get_target(e);
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        const char * txt = lv_label_get_text(label);
        
        if (strcmp(txt, "ADMIT") == 0 || strcmp(txt, "MANUAL UNLOCK") == 0) {
            trigger_door_unlock();
        } else if (strcmp(txt, "DENY") == 0) {
            ESP_LOGI(TAG, "Access Denied by User from Physical Display");
            const char* deny_msg = "CMD:DENY";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)deny_msg, strlen(deny_msg)); 
            
            if(xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                lv_obj_add_flag(ui_btn_admit, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_btn_deny, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(ui_lbl_status, "Access Denied.");
                lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0xFF0000), 0);
                xSemaphoreGiveRecursive(lvgl_mutex);
            }
        } else if (strcmp(txt, "LIVE VIDEO") == 0) {
            is_live_streaming = true;
            lv_label_set_text(label, "STOP STREAM");
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xF44336), 0);
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)"CMD:START_STREAM", 16);
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)"UI_MSG:STREAM_ON", 16);
        } else if (strcmp(txt, "STOP STREAM") == 0) {
            is_live_streaming = false;
            lv_label_set_text(label, "LIVE VIDEO");
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x4CAF50), 0);
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)"CMD:STOP_STREAM", 15);
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)"UI_MSG:STREAM_OFF", 17);
            
            if(xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                lv_label_set_text(ui_lbl_status, "System Idle. Stream Ended.");
                lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0xAAAAAA), 0);
                xSemaphoreGiveRecursive(lvgl_mutex);
            }
        }
    }
}

void build_ui() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x121212), LV_PART_MAIN);

    ui_lbl_status = lv_label_create(lv_scr_act());
    lv_label_set_text(ui_lbl_status, "Smart Doorbell - System Ready");
    lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_lbl_status, &lv_font_montserrat_14, 0); 
    lv_obj_align(ui_lbl_status, LV_ALIGN_TOP_MID, 0, 20);

    ui_img_cam = lv_img_create(lv_scr_act());
    lv_obj_set_size(ui_img_cam, 320, 240);
    lv_obj_align(ui_img_cam, LV_ALIGN_CENTER, 0, -40);
    
    img_sprite.setColorDepth(16);
    img_sprite.setPsram(true); 
    img_sprite.setSwapBytes(false); 
    if(!img_sprite.createSprite(320, 240)) {
        ESP_LOGE(TAG, "FATAL ERROR: Failed to allocate PSRAM for Camera Sprite!");
    }
    img_sprite.fillSprite(tft.color565(30, 30, 30)); 

    img_dsc.header.always_zero = 0; img_dsc.header.w = 320; img_dsc.header.h = 240;
    img_dsc.data_size = 320 * 240 * 2; img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc.data = (const uint8_t*)img_sprite.getBuffer();
    lv_img_set_src(ui_img_cam, &img_dsc);

    ui_btn_admit = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_admit, 120, 50);
    lv_obj_align(ui_btn_admit, LV_ALIGN_BOTTOM_LEFT, 40, -90);
    lv_obj_set_style_bg_color(ui_btn_admit, lv_color_hex(0x4CAF50), 0);
    lv_obj_add_event_cb(ui_btn_admit, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t * lbl_admit = lv_label_create(ui_btn_admit);
    lv_label_set_text(lbl_admit, "ADMIT");
    lv_obj_center(lbl_admit);
    lv_obj_add_flag(ui_btn_admit, LV_OBJ_FLAG_HIDDEN); 

    ui_btn_deny = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_deny, 120, 50);
    lv_obj_align(ui_btn_deny, LV_ALIGN_BOTTOM_RIGHT, -40, -90);
    lv_obj_set_style_bg_color(ui_btn_deny, lv_color_hex(0xF44336), 0);
    lv_obj_add_event_cb(ui_btn_deny, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t * lbl_deny = lv_label_create(ui_btn_deny);
    lv_label_set_text(lbl_deny, "DENY");
    lv_obj_center(lbl_deny);
    lv_obj_add_flag(ui_btn_deny, LV_OBJ_FLAG_HIDDEN); 

    // REORGANIZED BOTTOM BUTTONS
    ui_btn_unlock = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_unlock, 140, 50);
    lv_obj_align(ui_btn_unlock, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_set_style_bg_color(ui_btn_unlock, lv_color_hex(0x2196F3), 0);
    lv_obj_add_event_cb(ui_btn_unlock, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t * lbl_unlk = lv_label_create(ui_btn_unlock);
    lv_label_set_text(lbl_unlk, "MANUAL UNLOCK");
    lv_obj_center(lbl_unlk);

    ui_btn_stream = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ui_btn_stream, 140, 50);
    lv_obj_align(ui_btn_stream, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_set_style_bg_color(ui_btn_stream, lv_color_hex(0x4CAF50), 0);
    lv_obj_add_event_cb(ui_btn_stream, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t * lbl_str = lv_label_create(ui_btn_stream);
    lv_label_set_text(lbl_str, "LIVE VIDEO");
    lv_obj_center(lbl_str);
}

// ========================================================
// 4. DATABASE / SPIFFS LOGIC
// ========================================================
void init_spiffs() {
    esp_vfs_spiffs_conf_t conf = { .base_path = "/fr", .partition_label = "fr", .max_files = 10, .format_if_mount_failed = true };
    esp_vfs_spiffs_register(&conf);
    
    FILE* f = fopen("/fr/faces.db", "rb");
    if (f) {
        UserEmbedding temp;
        while (fread(&temp, sizeof(UserEmbedding), 1, f) == 1) {
            face_db.push_back(temp);
            ESP_LOGI(TAG, "Loaded Enrolled User: %s", temp.name);
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
    if (f) { fwrite(&new_user, sizeof(UserEmbedding), 1, f); fclose(f); }

    if (enroll_jpeg_buf) {
        char path[64]; snprintf(path, sizeof(path), "/fr/usr_%s.jpg", name);
        FILE* img_f = fopen(path, "wb");
        if (img_f) { 
            fwrite(enroll_jpeg_buf, 1, enroll_jpeg_len, img_f); 
            fclose(img_f); 
            ESP_LOGI(TAG, "Successfully saved enrollment image for %s.", name);
        }
        heap_caps_free(enroll_jpeg_buf);
        enroll_jpeg_buf = NULL;
        enroll_jpeg_len = 0;
    }
    ESP_LOGI(TAG, "Successfully enrolled: %s", name);
}

// ========================================================
// 5. ML INFERENCE TASK
// ========================================================
float calculate_cosine_similarity(float* a, float* b, int len) {
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for(int i = 0; i < len; i++) {
        dot += a[i] * b[i]; normA += a[i] * a[i]; normB += b[i] * b[i];
    }
    if(normA == 0.0f || normB == 0.0f) return 0.0f;
    return dot / (sqrt(normA) * sqrt(normB));
}

void align_crop_face_nn(uint8_t* src, int src_w, int src_h, const std::vector<int>& box, uint8_t* dest, int dest_w, int dest_h) {
    int x1 = box[0], y1 = box[1], x2 = box[2], y2 = box[3];
    if(x1 < 0) x1 = 0; if(y1 < 0) y1 = 0;
    if(x2 >= src_w) x2 = src_w - 1; if(y2 >= src_h) y2 = src_h - 1;
    
    int crop_w = x2 - x1 + 1; int crop_h = y2 - y1 + 1;
    if(crop_w <= 0 || crop_h <= 0) return;
    
    for (int y = 0; y < dest_h; y++) {
        for (int x = 0; x < dest_w; x++) {
            int src_x = x1 + (x * crop_w) / dest_w; 
            int src_y = y1 + (y * crop_h) / dest_h;
            if (src_x >= src_w) src_x = src_w - 1; if (src_y >= src_h) src_y = src_h - 1;
            int dest_idx = (y * dest_w + x) * 3; int src_idx = (src_y * src_w + src_x) * 3;
            dest[dest_idx] = src[src_idx]; dest[dest_idx + 1] = src[src_idx + 1]; dest[dest_idx + 2] = src[src_idx + 2];
        }
    }
}

void ml_inference_task(void *pvParameters) {
    ESP_LOGI(TAG, "ML Core Online.");
    HumanFaceDetectMSR01 detector(0.3F, 0.5F, 10, 1.0F); 
    FaceRecognition112V1S8 recognizer;

    static int64_t last_ml_run_time = 0;
    MLJob job;
    
    while (1) {
        if (xQueueReceive(ml_queue, &job, portMAX_DELAY)) {
            uint8_t* current_jpg = pp_buf[job.buf_idx];
            size_t current_len = job.jpeg_len;

            // 1. RESPONSIVE UI UPDATE (Draws EVERY frame instantly)
            if (xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                img_sprite.drawJpg(current_jpg, current_len, 0, 0);
                uint16_t* buf = (uint16_t*)img_sprite.getBuffer();
                if (buf != NULL) { 
                    for (int i = 0; i < 320 * 240; i++) buf[i] = (buf[i] >> 8) | (buf[i] << 8); 
                }
                lv_obj_invalidate(ui_img_cam);
                
                // Clear obsolete buttons instantly on new frame
                lv_obj_add_flag(ui_btn_admit, LV_OBJ_FLAG_HIDDEN); 
                lv_obj_add_flag(ui_btn_deny, LV_OBJ_FLAG_HIDDEN);  
                xSemaphoreGiveRecursive(lvgl_mutex);
            }

            // 2. THE ML FRAME DROPPER (Throttles AI to exactly 1 inference per second)
            bool run_ml = false;
            if (!is_live_streaming || (esp_timer_get_time() - last_ml_run_time > 1000000)) {
                run_ml = true;
                last_ml_run_time = esp_timer_get_time();
            }

            if (run_ml) {
                int64_t start_time = esp_timer_get_time();
                
                // If not streaming, send this one frame to dashboard
                if (!is_live_streaming) {
                    broadcast_ws_msg(HTTPD_WS_TYPE_BINARY, current_jpg, current_len);
                }

                int img_w = 320; int img_h = 240; 
                uint8_t *rgb_buf = (uint8_t *)heap_caps_malloc(img_w * img_h * 3, MALLOC_CAP_SPIRAM);
                bool dec_res = false;
                
                if (rgb_buf) dec_res = fmt2rgb888(current_jpg, current_len, PIXFORMAT_JPEG, rgb_buf);
                
                if (!dec_res) {
                    if(rgb_buf) free(rgb_buf);
                    continue;
                }

                std::list<dl::detect::result_t> &detect_results = detector.infer(rgb_buf, {img_h, img_w, 3});

                if (detect_results.size() > 0) {
                    if (face_db.empty() && current_mode == MODE_SECURITY) {
                        ESP_LOGW(TAG, "Database Empty! Unknown User Detected.");
                    }

                    uint8_t* aligned_face = (uint8_t*)heap_caps_malloc(112 * 112 * 3, MALLOC_CAP_SPIRAM);
                    align_crop_face_nn(rgb_buf, img_w, img_h, detect_results.front().box, aligned_face, 112, 112);

                    int8_t* input_int8 = (int8_t*)heap_caps_malloc(112 * 112 * 3, MALLOC_CAP_SPIRAM);
                    for(int i = 0; i < 112 * 112 * 3; i++) input_int8[i] = (int8_t)((int)aligned_face[i] - 128);

                    dl::Tensor<int8_t> input_tensor;
                    input_tensor.set_element(input_int8).set_shape({112, 112, 3}).set_auto_free(false);
                    
                    auto& feature = recognizer.forward(input_tensor);
                    int8_t* feature_ptr_int8 = (int8_t*)feature.get_element_ptr();

                    float feature_ptr[128];
                    for(int i = 0; i < 128; i++) feature_ptr[i] = (float)feature_ptr_int8[i];

                    if (current_mode == MODE_ENROLL) {
                        float highest_similarity = 0.0f; 
                        char best_match[32] = "";
                        for (const auto& user : face_db) {
                            float similarity = calculate_cosine_similarity(feature_ptr, (float*)user.feature, 128);
                            if (similarity > highest_similarity) { highest_similarity = similarity; strcpy(best_match, user.name); }
                        }

                        if (highest_similarity > 0.80f) {
                            ESP_LOGI(TAG, "User already registered as %s. Confidence: %.2f", best_match, highest_similarity);

                            char msg[64]; snprintf(msg, sizeof(msg), "UI_MSG:DUPLICATE:%s", best_match);
                            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
                            
                            if (xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                                char stat_msg[64]; snprintf(stat_msg, sizeof(stat_msg), "Duplicate! Known as: %s", best_match);
                                lv_label_set_text(ui_lbl_status, stat_msg);
                                lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0xFFA500), 0);
                                xSemaphoreGiveRecursive(lvgl_mutex);
                            }
                        } else {
                            ESP_LOGI(TAG, "New face detected for enrollment. Confidence against DB: %.2f", highest_similarity);
                            memcpy(pending_feature, feature_ptr, sizeof(float) * 128);

                            if (enroll_jpeg_buf) heap_caps_free(enroll_jpeg_buf);
                            enroll_jpeg_buf = (uint8_t*)heap_caps_malloc(current_len, MALLOC_CAP_SPIRAM);
                            if (enroll_jpeg_buf) {
                                memcpy(enroll_jpeg_buf, current_jpg, current_len);
                                enroll_jpeg_len = current_len;
                            }

                            const char* msg = "UI_MSG:NEW_FACE";
                            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));

                            if (xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                                lv_label_set_text(ui_lbl_status, "New Face! Check Dashboard to Save.");
                                lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0x00BFFF), 0);
                                xSemaphoreGiveRecursive(lvgl_mutex);
                            }
                        }
                    } 
                    else if (current_mode == MODE_SECURITY) {
                        float highest_similarity = 0.0f; 
                        char best_match[32] = "Unknown";

                        for (const auto& user : face_db) {
                            float similarity = calculate_cosine_similarity(feature_ptr, (float*)user.feature, 128);
                            if (similarity > highest_similarity) { highest_similarity = similarity; strcpy(best_match, user.name); }
                        }

                        if (highest_similarity < 0.55f) { 
                            strcpy(best_match, "Unknown"); 
                            ESP_LOGI(TAG, "Unknown face detected. Confidence: %.2f", highest_similarity);
                        } else {
                            ESP_LOGI(TAG, "User recognized as %s. Confidence: %.2f", best_match, highest_similarity);
                        }

                        char ws_msg[64];
                        snprintf(ws_msg, sizeof(ws_msg), "UI_MSG:MATCH:%s", best_match);
                        if(strcmp(best_match, "Unknown") == 0) strcpy(ws_msg, "UI_MSG:UNKNOWN");
                        broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)ws_msg, strlen(ws_msg));

                        if (xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                            char status_text[64];
                            if (strcmp(best_match, "Unknown") == 0) {
                                snprintf(status_text, sizeof(status_text), "Intruder Alert!");
                                lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0xFF0000), 0);
                                lv_obj_clear_flag(ui_btn_admit, LV_OBJ_FLAG_HIDDEN); 
                                lv_obj_clear_flag(ui_btn_deny, LV_OBJ_FLAG_HIDDEN);  
                            } else {
                                snprintf(status_text, sizeof(status_text), "Welcome, %s!", best_match);
                                lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0x00FF00), 0);
                                
                                // AUTO-UNLOCK SUPPRESSION: Only auto-unlock if NOT in streaming mode
                                if (!is_live_streaming) {
                                    const char* unlock_msg = "CMD:UNLOCK";
                                    broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)unlock_msg, strlen(unlock_msg));
                                }
                            }
                            lv_label_set_text(ui_lbl_status, status_text);
                            xSemaphoreGiveRecursive(lvgl_mutex);
                        }
                    }
                    free(aligned_face);
                    free(input_int8);
                } else {
                    const char* msg = "UI_MSG:NO_FACE";
                    broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));

                    if (xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                        lv_label_set_text(ui_lbl_status, is_live_streaming ? "Streaming Live. No face detected." : "System Idle. No face detected.");
                        lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0xAAAAAA), 0);
                        xSemaphoreGiveRecursive(lvgl_mutex);
                    }
                }
                
                free(rgb_buf);
                int64_t end_time = esp_timer_get_time();
                ESP_LOGI(TAG, "ML Pipeline ran in %.2f seconds", (float)(end_time - start_time) / 1000000.0f);
            }
        }
    }
}

// ========================================================
// 6. HTTP SERVER & WEBSOCKETS (DASHBOARD)
// ========================================================
const char* index_html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <link rel="icon" href="data:,">
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Doorbell Hub</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #121212; color: #ffffff; margin: 0; padding: 0; }
        .header { background-color: #1e1e1e; padding: 20px; text-align: center; border-bottom: 2px solid #333; }
        .tabs { display: flex; justify-content: center; background-color: #1a1a1a; }
        .tab { padding: 15px 30px; cursor: pointer; border-bottom: 3px solid transparent; font-weight: bold; transition: 0.3s; }
        .tab:hover { background-color: #2a2a2a; }
        .tab.active { border-bottom: 3px solid #4CAF50; color: #4CAF50; }
        .content { display: none; padding: 20px; max-width: 600px; margin: auto; text-align: center; }
        .content.active { display: block; }
        
        .toggle-container { margin: 20px 0; background: #222; padding: 15px; border-radius: 8px; }
        .switch { position: relative; display: inline-block; width: 60px; height: 34px; vertical-align: middle; margin-left: 10px; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #4CAF50; transition: .4s; border-radius: 34px; }
        .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
        input:checked + .slider { background-color: #2196F3; }
        input:checked + .slider:before { transform: translateX(26px); }

        .cam-feed { width: 100%; max-width: 320px; height: 240px; background-color: #000; border: 2px solid #4CAF50; border-radius: 8px; margin: 20px auto; object-fit: cover; }
        .status-box { font-size: 1.2em; padding: 15px; margin: 20px 0; background: #1e1e1e; border-radius: 8px; border-left: 5px solid #4CAF50; }
        
        button { background-color: #4CAF50; color: white; border: none; padding: 10px 20px; font-size: 1em; cursor: pointer; border-radius: 5px; margin: 5px; transition: 0.3s; }
        button:hover { background-color: #45a049; }
        .btn-danger { background-color: #f44336; } .btn-danger:hover { background-color: #da190b; }
        
        #controls { display: none; margin-top: 15px; }

        .user-card { display: inline-block; width: 110px; background: #222; border-radius: 8px; margin-right: 10px; text-align: center; padding: 10px; border: 1px solid #333; }
        .user-card img { width: 90px; height: 90px; border-radius: 5px; object-fit: cover; }
        .user-scroll { overflow-x: auto; white-space: nowrap; padding-bottom: 10px; margin-bottom: 20px; text-align: left;}
        .gallery { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-top: 10px; margin-bottom: 20px;}
        .gallery img { width: 100%; aspect-ratio: 4/3; object-fit: cover; border-radius: 5px; border: 1px solid #444; }
        .log-table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 0.9em; }
        .log-table th, .log-table td { border: 1px solid #444; padding: 8px; text-align: left; }
        .log-table th { background-color: #333; }
        h3 { text-align: left; border-bottom: 1px solid #333; padding-bottom: 5px; }
    </style>
</head>
<body>

    <div class="header">
        <h2>Smart Doorbell Hub</h2>
        <p id="ws-status" style="color: #ff9800;">Connecting...</p>
    </div>

    <div class="tabs">
        <div class="tab active" onclick="switchTab('dash')">Dashboard</div>
        <div class="tab" onclick="switchTab('admin')">Admin Panel</div>
    </div>

    <div id="dash" class="content active">
        <div class="toggle-container">
            <strong>Mode:</strong> <span id="modeLabel">SECURITY</span>
            <label class="switch">
                <input type="checkbox" id="modeToggle" onchange="toggleMode()">
                <span class="slider"></span>
            </label>
        </div>

        <img id="camImage" class="cam-feed" src="" alt="Waiting for Camera...">
        
        <div id="statusBox" class="status-box">System Idle. Waiting for trigger...</div>
        
        <div id="controls"></div>

        <hr style="border-color: #333; margin: 30px 0;">
        <button onclick="manualUnlock()" style="width: 100%; padding: 15px; font-size: 1.2em;">🔓 MANUAL UNLOCK</button>
    </div>

    <div id="admin" class="content">
        <h3>Enrolled Users</h3>
        <div class="user-scroll" id="userList"></div>

        <h3>Recent Captures (Past 12)</h3>
        <div class="gallery" id="gallery"></div>

        <h3>Visitor Logs</h3>
        <button onclick="window.open('/logs.csv')" style="float: left; margin-bottom: 10px;">📥 Export CSV</button>
        <div style="max-height: 200px; overflow-y: auto; clear: both;">
            <table class="log-table">
                <thead><tr><th>Time</th><th>Event</th><th>Person</th></tr></thead>
                <tbody id="logBody"></tbody>
            </table>
        </div>

        <hr style="border-color: #333; margin: 30px 0;">
        <p>Use this to clear history without deleting enrolled users.</p>
        <button class="btn-danger" style="width: 100%; padding: 15px; margin-bottom: 10px;" onclick="wipeLogs()">🗑️ DELETE ALL LOGS & CAPTURES</button>
        
        <p>Use this to wipe all enrolled faces from the system.</p>
        <button class="btn-danger" style="width: 100%; padding: 15px;" onclick="wipeDB()">⚠️ DELETE ALL ENROLLMENTS</button>
    </div>

    <script>
        let ws;
        let pastCaptures = [];
        let isStreaming = false; // FIX: Global stream tracker

        function initWS() {
            ws = new WebSocket('ws://' + location.host + '/ws');
            ws.binaryType = 'blob';
            
            ws.onopen = () => {
                document.getElementById('ws-status').innerHTML = '<span style="color:#4CAF50;">Live</span>';
                fetchUsers();
            };
            ws.onclose = () => { document.getElementById('ws-status').innerHTML = '<span style="color:#f44336;">Disconnected. Reconnecting...</span>'; setTimeout(initWS, 2000); };
            
            ws.onmessage = function(e) {
                if (typeof e.data === 'string') {
                    handleCommand(e.data);
                } else {
                    let imgUrl = URL.createObjectURL(e.data);
                    document.getElementById('camImage').src = imgUrl;
                    
                    pastCaptures.unshift(imgUrl);
                    if(pastCaptures.length > 12) pastCaptures.pop();
                    updateGallery();
                }
            };
        }

        function handleCommand(cmd) {
            let status = document.getElementById('statusBox');
            let controls = document.getElementById('controls');
            let dateStr = new Date().toLocaleTimeString();

            if (cmd === "UI_MSG:NEW_FACE") {
                status.innerHTML = `[${dateStr}] New Face Detected!`;
                controls.innerHTML = `<button onclick="acceptFace()">Accept</button> <button class="btn-danger" onclick="resetControls()">Retake</button>`;
                controls.style.display = "block";
            } else if (cmd.startsWith("UI_MSG:DUPLICATE:")) {
                status.innerHTML = `[${dateStr}] Already Enrolled as <b>${cmd.split(":")[2]}</b>`;
                resetControls();
            } else if (cmd.startsWith("UI_MSG:MATCH:")) {
                let name = cmd.split(":")[2];
                status.innerHTML = `[${dateStr}] ✅ Welcome, <b>${name}</b>! Door Unlocked.`;
                logEvent("MATCH", name);
                resetControls();
            } else if (cmd === "UI_MSG:UNKNOWN") {
                status.innerHTML = `[${dateStr}] ❌ UNKNOWN PERSON DETECTED!`;
                controls.innerHTML = `<button onclick="manualAdmit()">Admit</button> <button class="btn-danger" onclick="denyAccess()">Deny</button>`;
                controls.style.display = "block";
            } else if (cmd === "UI_MSG:NO_FACE") {
                status.innerHTML = `[${dateStr}] No Face Detected in image.`;
                resetControls();
            } else if (cmd === "UI_MSG:ENROLL_SUCCESS") {
                status.innerHTML = `[${dateStr}] ✅ Enrollment Successful! Waiting for next...`;
                fetchUsers();
                resetControls();
            } else if (cmd === "UI_MSG:STREAM_ON") {
                isStreaming = true;
                status.innerHTML = `[${dateStr}] 🎥 Live Video Streaming to Display...`;
                resetControls();
            } else if (cmd === "UI_MSG:STREAM_OFF") {
                isStreaming = false;
                status.innerHTML = `[${dateStr}] 🛑 Stream Ended. System Idle.`;
                resetControls();
            } else if (cmd === "CMD:UNLOCK" || cmd === "CMD:DENY") {
                resetControls(); 
            } else if (cmd === "UI_MSG:LOGS_WIPED") {
                pastCaptures = [];
                updateGallery();
                document.getElementById('logBody').innerHTML = '';
                alert("Logs and captures successfully cleared.");   
            } else if (cmd === "UI_MSG:DB_WIPED") {
                fetchUsers();
                alert("Database successfully wiped.");
            } else if (cmd.startsWith("UI_MSG:USERS:")) {
                renderUsers(cmd.substring(13));
            } else if (cmd === "UI_MSG:USER_DELETED") {
                fetchUsers();
            }
        }

        function toggleMode() {
            let isEnroll = document.getElementById('modeToggle').checked;
            
            // FIX: Prevent mode switching while stream is active
            if (isStreaming && isEnroll) {
                alert("Please stop the live video stream before switching to Enrollment mode.");
                document.getElementById('modeToggle').checked = false; // Revert switch
                return;
            }
            
            document.getElementById('modeLabel').innerText = isEnroll ? "ENROLLMENT" : "SECURITY";
            document.getElementById('statusBox').innerText = isEnroll ? "Waiting for new enrollment..." : "System Idle. Waiting for trigger...";
            resetControls();
            ws.send(isEnroll ? "CMD:MODE_ENROLL" : "CMD:MODE_SECURITY");
        }
        function acceptFace() {
            let name = prompt("Enter name for this user:");
            if(name && name.trim() !== "") ws.send("CMD:SAVE_NAME:" + name.trim());
        }
        function manualUnlock() {
            ws.send("CMD:UNLOCK");
            logEvent("MANUAL UNLOCK", "Admin");
            document.getElementById('statusBox').innerHTML = "🔓 Door manually unlocked!";
            resetControls();
        }
        function manualAdmit() {
            ws.send("CMD:UNLOCK");
            logEvent("MANUAL ADMIT", "Unknown");
            document.getElementById('statusBox').innerHTML = "🔓 Unknown Person Admitted manually.";
            resetControls();
        }
        function denyAccess() {
            logEvent("DENIED", "Unknown");
            document.getElementById('statusBox').innerHTML = "❌ Access Denied.";
            ws.send("CMD:DENY"); 
            resetControls();
        }
        function resetControls() { document.getElementById('controls').style.display = "none"; }
        
        function switchTab(tabId) {
            // FIX: Auto-stop stream when leaving dashboard
            if (tabId === 'admin' && isStreaming) {
                ws.send("CMD:STOP_STREAM");
            }
            
            document.querySelectorAll('.content').forEach(c => c.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.getElementById(tabId).classList.add('active');
            event.target.classList.add('active');
        }

        function fetchUsers() { ws.send("CMD:GET_USERS"); }
        function renderUsers(userStr) {
            let list = document.getElementById('userList');
            if(!userStr || userStr === "") { list.innerHTML = "<p>No enrolled users.</p>"; return; }
            let users = userStr.split(',');
            let html = "";
            users.forEach(u => {
                if(u.trim() !== "") {
                    let cacheBuster = new Date().getTime();
                    html += `<div class="user-card">
                                <img src="/img?u=${u}&cb=${cacheBuster}" alt="${u}" onerror="this.src='data:image/svg+xml;utf8,<svg xmlns=http://www.w3.org/2000/svg width=100 height=100><rect width=100 height=100 fill=%23444/></svg>'">
                                <p style="margin: 5px 0;">${u}</p>
                                <button class="btn-danger" style="padding: 5px; width: 100%; font-size: 0.9em;" onclick="deleteUser('${u}')">Delete</button>
                             </div>`;
                }
            });
            list.innerHTML = html;
        }
        function deleteUser(name) { if(confirm("Delete " + name + " from database?")) ws.send("CMD:DELETE_USER:" + name); }
        function updateGallery() { document.getElementById('gallery').innerHTML = pastCaptures.map(src => `<img src="${src}">`).join(''); }
        function logEvent(evt, person) {
            let d = new Date(); let ts = d.toLocaleDateString() + " " + d.toLocaleTimeString();
            ws.send(`CMD:LOG:${ts},${evt},${person}`);
            document.getElementById('logBody').innerHTML = `<tr><td>${ts}</td><td>${evt}</td><td>${person}</td></tr>` + document.getElementById('logBody').innerHTML;
        }
        function wipeDB() { if(confirm("Are you sure? This will delete all faces permanently!")) ws.send("CMD:WIPE_DB"); }
        function wipeLogs() { if(confirm("Are you sure you want to clear all logs and recent images?")) ws.send("CMD:WIPE_LOGS"); }

        window.onload = initWS;
    </script>
</body>
</html>
)rawliteral";

esp_err_t http_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t img_get_handler(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char name[32];
        if (httpd_query_key_value(buf, "u", name, sizeof(name)) == ESP_OK) {
            char path[64]; snprintf(path, sizeof(path), "/fr/usr_%s.jpg", name);
            FILE* f = fopen(path, "rb");
            if (f) {
                httpd_resp_set_type(req, "image/jpeg");
                char chunk[1024]; size_t read_bytes;
                while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) httpd_resp_send_chunk(req, chunk, read_bytes);
                fclose(f);
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

esp_err_t logs_get_handler(httpd_req_t *req) {
    FILE* f = fopen("/fr/logs.csv", "rb");
    if (!f) { httpd_resp_send(req, "Time,Event,Person\n", HTTPD_RESP_USE_STRLEN); return ESP_OK; }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"visitor_logs.csv\"");
    char chunk[1024]; size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) httpd_resp_send_chunk(req, chunk, read_bytes);
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;
    httpd_ws_frame_t ws_pkt = {}; 
    
    if (httpd_ws_recv_frame(req, &ws_pkt, 0) != ESP_OK || ws_pkt.len == 0) return ESP_FAIL;

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        char* text = (char*)ws_pkt.payload;
        
        if (strncmp(text, "CMD:MODE_ENROLL", 15) == 0) {
            current_mode = MODE_ENROLL;
            ESP_LOGI(TAG, "Mode set to ENROLLMENT"); 
            if(xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                lv_obj_add_flag(ui_btn_unlock, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_btn_stream, LV_OBJ_FLAG_HIDDEN);
                xSemaphoreGiveRecursive(lvgl_mutex);
            }
        } else if (strncmp(text, "CMD:MODE_SECURITY", 17) == 0) {
            current_mode = MODE_SECURITY;
            ESP_LOGI(TAG, "Mode set to SECURITY"); 
            if(xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                lv_obj_clear_flag(ui_btn_unlock, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(ui_btn_stream, LV_OBJ_FLAG_HIDDEN);
                xSemaphoreGiveRecursive(lvgl_mutex);
            }
        } else if (strncmp(text, "CMD:SAVE_NAME:", 14) == 0) {
            char* name = text + 14;
            save_to_spiffs(name, pending_feature);
            const char* msg = "UI_MSG:ENROLL_SUCCESS";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
            if(xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                lv_label_set_text(ui_lbl_status, "Enrollment Success. System Armed.");
                lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0xFFFFFF), 0);
                xSemaphoreGiveRecursive(lvgl_mutex);
            }
        } else if (strncmp(text, "CMD:GET_USERS", 13) == 0) {
            char users[512] = "UI_MSG:USERS:";
            for (const auto& user : face_db) { 
                strncat(users, user.name, sizeof(users) - strlen(users) - 1); 
                strncat(users, ",", sizeof(users) - strlen(users) - 1); 
            }
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)users, strlen(users));
        } else if (strncmp(text, "CMD:DELETE_USER:", 16) == 0) {
            char* name = text + 16;
            for (auto it = face_db.begin(); it != face_db.end(); ++it) {
                if (strcmp(it->name, name) == 0) { face_db.erase(it); break; }
            }
            FILE* f = fopen("/fr/faces.db", "wb");
            if (f) { for (const auto& user : face_db) fwrite(&user, sizeof(UserEmbedding), 1, f); fclose(f); }
            char path[64]; snprintf(path, sizeof(path), "/fr/usr_%s.jpg", name); remove(path);
            const char* msg = "UI_MSG:USER_DELETED";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
        } else if (strncmp(text, "CMD:LOG:", 8) == 0) {
            FILE* f = fopen("/fr/logs.csv", "ab");
            if (f) { fprintf(f, "%s\n", text + 8); fclose(f); }
        } else if (strncmp(text, "CMD:WIPE_LOGS", 13) == 0) {
            remove("/fr/logs.csv");
            const char* msg = "UI_MSG:LOGS_WIPED";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
        } else if (strncmp(text, "CMD:WIPE_DB", 11) == 0) {
            for (const auto& user : face_db) { char path[64]; snprintf(path, sizeof(path), "/fr/usr_%s.jpg", user.name); remove(path); }
            face_db.clear(); remove("/fr/faces.db");
            const char* msg = "UI_MSG:DB_WIPED";
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)msg, strlen(msg));
        } else if (strncmp(text, "CMD:STOP_STREAM", 15) == 0) {
            is_live_streaming = false;
            ESP_LOGI(TAG, "Dashboard trigger: STOP STREAM");
            if(xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                lv_label_set_text(lv_obj_get_child(ui_btn_stream, 0), "LIVE VIDEO");
                lv_obj_set_style_bg_color(ui_btn_stream, lv_color_hex(0x4CAF50), 0);
                lv_label_set_text(ui_lbl_status, "System Idle. Stream Ended.");
                lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0xAAAAAA), 0);
                xSemaphoreGiveRecursive(lvgl_mutex);
            }
            // Forward commands to sync dashboard and camera
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)"UI_MSG:STREAM_OFF", 17);
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)"CMD:STOP_STREAM", 15);
        } else if (strncmp(text, "CMD:UNLOCK", 10) == 0) {
            trigger_door_unlock();
        } else if (strncmp(text, "CMD:DENY", 8) == 0) {
            ESP_LOGI(TAG, "Dashboard trigger: DENY");
            if(xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
                lv_obj_add_flag(ui_btn_admit, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_btn_deny, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(ui_lbl_status, "System Idle. Waiting for trigger...");
                lv_obj_set_style_text_color(ui_lbl_status, lv_color_hex(0xAAAAAA), 0);
                xSemaphoreGiveRecursive(lvgl_mutex);
            }
            broadcast_ws_msg(HTTPD_WS_TYPE_TEXT, (uint8_t*)"CMD:DENY", 8); 
        }

        free(buf);
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
        if (ws_pkt.len > PING_PONG_SIZE) {
            ESP_LOGW(TAG, "Frame too large for Ping-Pong buffer (%d bytes)", ws_pkt.len);
            return ESP_FAIL;
        }

        // PING PONG LOGIC: Eliminates 10 FPS memory fragmentation
        ws_pkt.payload = pp_buf[pp_write_idx];
        httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

        MLJob job; 
        job.buf_idx = pp_write_idx; 
        job.jpeg_len = ws_pkt.len;
        
        if (xQueueSend(ml_queue, &job, 0) == pdTRUE) {
            pp_write_idx = (pp_write_idx + 1) % 3; // CYCLE THROUGH 3 BUFFERS
        }
    }
    return ESP_OK;
}

httpd_handle_t start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7; 
    config.stack_size = 10240; 

    httpd_uri_t uri_get = {}; uri_get.uri = "/"; uri_get.method = HTTP_GET; uri_get.handler = http_get_handler;
    httpd_uri_t img_get = {}; img_get.uri = "/img"; img_get.method = HTTP_GET; img_get.handler = img_get_handler;
    httpd_uri_t logs_get = {}; logs_get.uri = "/logs.csv"; logs_get.method = HTTP_GET; logs_get.handler = logs_get_handler;
    httpd_uri_t ws_uri = {}; ws_uri.uri = "/ws"; ws_uri.method = HTTP_GET; ws_uri.handler = ws_handler; ws_uri.is_websocket = true;

    if (httpd_start(&global_server, &config) == ESP_OK) {
        httpd_register_uri_handler(global_server, &uri_get);
        httpd_register_uri_handler(global_server, &img_get);
        httpd_register_uri_handler(global_server, &logs_get);
        httpd_register_uri_handler(global_server, &ws_uri);
        return global_server;
    }
    return NULL;
}

void wifi_init_softap() {
    esp_netif_init(); esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, "ESP32_ML_BRAIN"); strcpy((char*)wifi_config.ap.password, "12345678");
    wifi_config.ap.ssid_len = strlen("ESP32_ML_BRAIN");
    wifi_config.ap.channel = 1; wifi_config.ap.max_connection = 4; wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_AP); esp_wifi_set_config(WIFI_IF_AP, &wifi_config); esp_wifi_start();
}

// ========================================================
// 7. MAIN APP ENTRY
// ========================================================
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "--- Booting Hub ---");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); ret = nvs_flash_init();
    }
    init_spiffs();
    wifi_init_softap();

    // ALLOCATE TRIPLE BUFFERS IN PSRAM
    pp_buf[0] = (uint8_t*)heap_caps_malloc(PING_PONG_SIZE, MALLOC_CAP_SPIRAM);
    pp_buf[1] = (uint8_t*)heap_caps_malloc(PING_PONG_SIZE, MALLOC_CAP_SPIRAM);
    pp_buf[2] = (uint8_t*)heap_caps_malloc(PING_PONG_SIZE, MALLOC_CAP_SPIRAM);
    
    ml_queue = xQueueCreate(1, sizeof(MLJob)); // STRICT SIZE 1 TO PREVENT CORRUPTION
    lvgl_mutex = xSemaphoreCreateRecursiveMutex(); 

    ESP_LOGI(TAG, "Initializing TFT & Touch...");
    tft.init(); tft.setBrightness(200);

    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();
    static lv_disp_draw_buf_t draw_buf;
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(480 * 40 * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 480 * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 480; disp_drv.ver_res = 480;
    disp_drv.flush_cb = my_disp_flush; disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touch_read;
    lv_indev_drv_register(&indev_drv);

    esp_timer_create_args_t periodic_timer_args = {};
    periodic_timer_args.callback = &lv_tick_task;
    periodic_timer_args.name = "periodic_gui";
    esp_timer_handle_t periodic_timer;
    esp_timer_create(&periodic_timer_args, &periodic_timer);
    esp_timer_start_periodic(periodic_timer, 2 * 1000); 

    ESP_LOGI(TAG, "Building LVGL UI...");
    if (xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
        build_ui();
        xSemaphoreGiveRecursive(lvgl_mutex);
    }

    start_webserver();

    xTaskCreatePinnedToCore(ml_inference_task, "ML_Task", 1024 * 16, NULL, 5, NULL, 1);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY)) {
            lv_timer_handler();
            xSemaphoreGiveRecursive(lvgl_mutex);
        }
    }
}