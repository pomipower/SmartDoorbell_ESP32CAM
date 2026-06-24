#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

// Graphics & UI
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include "lvgl.h"

static const char *TAG = "UI_TEST";

// ========================================================
// 1. HARDWARE & DISPLAY CONFIGURATION (From Test 07)
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
    lgfx::Touch_FT5x06 _touch_instance; // ADDED FT5x06 TOUCH DRIVER!

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
// 2. LVGL INTEGRATION & UI
// ========================================================
static SemaphoreHandle_t lvgl_mutex;

// LovyanGFX -> LVGL Display Flush
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}

// LovyanGFX -> LVGL Touch Read
static void my_touch_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint16_t touchX, touchY;
    bool touched = tft.getTouch(&touchX, &touchY);
    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
    }
}

// LVGL Tick Task
static void lv_tick_task(void *arg) {
    lv_tick_inc(2); // 2ms tick
}

lv_obj_t * ui_img_cam; // Global image object
lv_img_dsc_t img_dsc;  // Global image descriptor
LGFX_Sprite img_sprite(&tft); // The magic buffer!

httpd_handle_t global_server = NULL;

// Broadcast WS message
void broadcast_ws_msg(const char *msg) {
    if(!global_server) return;
    size_t max_clients = 10; int client_fds[10];
    if (httpd_get_client_list(global_server, &max_clients, client_fds) == ESP_OK) {
        for (size_t i = 0; i < max_clients; i++) {
            httpd_ws_frame_t ws_pkt = {};
            ws_pkt.payload = (uint8_t*)msg; ws_pkt.len = strlen(msg); ws_pkt.type = HTTPD_WS_TYPE_TEXT;
            httpd_ws_send_frame_async(global_server, client_fds[i], &ws_pkt);
        }
    }
}

// Button Callback
static void btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        lv_obj_t * btn = lv_event_get_target(e);
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        const char * txt = lv_label_get_text(label);
        
        ESP_LOGI(TAG, "Touch Registered: %s Pressed!", txt);
        char msg[32]; snprintf(msg, sizeof(msg), "CMD:%s", txt);
        broadcast_ws_msg(msg);
    }
}

void build_ui() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x121212), LV_PART_MAIN);

    // Header
    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Smart Doorbell - Live Feed");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

    // Image Container (320x240)
    ui_img_cam = lv_img_create(lv_scr_act());
    lv_obj_set_size(ui_img_cam, 320, 240);
    lv_obj_align(ui_img_cam, LV_ALIGN_CENTER, 0, -20);
    
    // Allocate the sprite as our JPEG decode buffer
    img_sprite.setColorDepth(16);
    img_sprite.setSwapBytes(false); // Aligns JPEG endianness with LVGL naturally
    img_sprite.createSprite(320, 240);
    img_sprite.fillSprite(tft.color565(50, 50, 50)); // Dark grey placeholder

    img_dsc.header.always_zero = 0;
    img_dsc.header.w = 320;
    img_dsc.header.h = 240;
    img_dsc.data_size = 320 * 240 * 2;
    img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc.data = (const uint8_t*)img_sprite.getBuffer();
    lv_img_set_src(ui_img_cam, &img_dsc);

    // Admit Button
    lv_obj_t * btn_admit = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_admit, 140, 60);
    lv_obj_align(btn_admit, LV_ALIGN_BOTTOM_LEFT, 40, -30);
    lv_obj_set_style_bg_color(btn_admit, lv_color_hex(0x4CAF50), 0);
    lv_obj_add_event_cb(btn_admit, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t * lbl_admit = lv_label_create(btn_admit);
    lv_label_set_text(lbl_admit, "ADMIT");
    lv_obj_center(lbl_admit);

    // Deny Button
    lv_obj_t * btn_deny = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_deny, 140, 60);
    lv_obj_align(btn_deny, LV_ALIGN_BOTTOM_RIGHT, -40, -30);
    lv_obj_set_style_bg_color(btn_deny, lv_color_hex(0xF44336), 0);
    lv_obj_add_event_cb(btn_deny, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t * lbl_deny = lv_label_create(btn_deny);
    lv_label_set_text(lbl_deny, "DENY");
    lv_obj_center(lbl_deny);
}

// ========================================================
// 3. WEBSOCKET & NETWORK
// ========================================================
esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    
    if (httpd_ws_recv_frame(req, &ws_pkt, 0) != ESP_OK || ws_pkt.len == 0) return ESP_FAIL;

    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
        uint8_t* jpeg_buf = (uint8_t*)heap_caps_malloc(ws_pkt.len, MALLOC_CAP_SPIRAM);
        ws_pkt.payload = jpeg_buf;
        httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

        ESP_LOGI(TAG, "Received JPEG (%d bytes). Decoding to Sprite...", ws_pkt.len);

        if (xSemaphoreTake(lvgl_mutex, portMAX_DELAY)) {
            img_sprite.drawJpg(jpeg_buf, ws_pkt.len, 0, 0);
            
            // THE FIX: Manually byte-swap the buffer from Big-Endian to Little-Endian for LVGL
            uint16_t* buf = (uint16_t*)img_sprite.getBuffer();
            for (int i = 0; i < 320 * 240; i++) {
                buf[i] = (buf[i] >> 8) | (buf[i] << 8); 
            }
            
            lv_obj_invalidate(ui_img_cam);
            xSemaphoreGive(lvgl_mutex);
        }
        free(jpeg_buf);
    }
    return ESP_OK;
}

void start_softap() {
    esp_netif_init(); esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, "ESP32_ML_BRAIN");
    strcpy((char*)wifi_config.ap.password, "12345678");
    wifi_config.ap.ssid_len = strlen("ESP32_ML_BRAIN");
    wifi_config.ap.channel = 1; wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_AP); esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t ws_uri = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .user_ctx = NULL, .is_websocket = true };
    if (httpd_start(&global_server, &config) == ESP_OK) {
        httpd_register_uri_handler(global_server, &ws_uri);
    }
}

// ========================================================
// 4. MAIN ENTRY POINT
// ========================================================
extern "C" void app_main() {
    ESP_LOGI(TAG, "Booting...");
    nvs_flash_init();
    lvgl_mutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "Initializing TFT & Touch...");
    tft.init();
    tft.setBrightness(200);

    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();
    
    static lv_disp_draw_buf_t draw_buf;
    // THE FIX: Moved the 38KB draw buffer to Internal RAM. This completely unblocks the PSRAM bus!
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(480 * 40 * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 480 * 40);
    
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 480; disp_drv.ver_res = 480;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
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
    esp_timer_start_periodic(periodic_timer, 2 * 1000); // 2ms

    ESP_LOGI(TAG, "Building UI...");
    if (xSemaphoreTake(lvgl_mutex, portMAX_DELAY)) {
        build_ui();
        xSemaphoreGive(lvgl_mutex);
    }

    ESP_LOGI(TAG, "Starting Wi-Fi & WebSockets...");
    start_softap();

    // Main LVGL Task Loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (xSemaphoreTake(lvgl_mutex, portMAX_DELAY)) {
            lv_timer_handler();
            xSemaphoreGive(lvgl_mutex);
        }
    }
}