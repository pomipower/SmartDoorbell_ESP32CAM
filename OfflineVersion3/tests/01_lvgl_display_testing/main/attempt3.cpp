#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

// 1. LVGL must be included BEFORE LovyanGFX
#include "lvgl.h"

// 2. Include LovyanGFX Base + ESP32-S3 specific hardware drivers
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>

static const char *TAG = "S3_PANELLAN_EXACT";

// ========================================================
// 1. PANELLAN EXACT SPI BOOTLOADER (From bc02_rgb_spi_init.hpp)
// ========================================================
static spi_device_handle_t g_screen_spi;

static void lcd_cmd(spi_device_handle_t spi, const uint16_t data) {
    uint16_t tmp_cmd = (data | 0x0000);
    spi_transaction_ext_t trans = {};
    trans.base.flags = SPI_TRANS_VARIABLE_CMD;
    trans.base.cmd = tmp_cmd;
    trans.command_bits = 9;
    ESP_ERROR_CHECK(spi_device_transmit(spi, (spi_transaction_t*)&trans));
}

static void lcd_data(spi_device_handle_t spi, const uint16_t data) {
    uint16_t tmp_cmd = (data | 0x0100);
    spi_transaction_ext_t trans = {};
    trans.base.flags = SPI_TRANS_VARIABLE_CMD;
    trans.base.cmd = tmp_cmd;
    trans.command_bits = 9;
    ESP_ERROR_CHECK(spi_device_transmit(spi, (spi_transaction_t*)&trans));
}

static void SPI_WriteComm(uint16_t cmd) { lcd_cmd(g_screen_spi, cmd); }
static void SPI_WriteData(uint16_t data) { lcd_data(g_screen_spi, data); }

static void rgb_driver_init(void) {
    SPI_WriteComm(0xF0); SPI_WriteData(0x55); SPI_WriteData(0xAA); SPI_WriteData(0x52); SPI_WriteData(0x08); SPI_WriteData(0x00);
    SPI_WriteComm(0xF6); SPI_WriteData(0x5A); SPI_WriteData(0x87);
    SPI_WriteComm(0xC1); SPI_WriteData(0x3F);
    SPI_WriteComm(0xC2); SPI_WriteData(0x0E);
    SPI_WriteComm(0xC6); SPI_WriteData(0xF8);
    SPI_WriteComm(0xC9); SPI_WriteData(0x10);
    SPI_WriteComm(0xCD); SPI_WriteData(0x25);
    SPI_WriteComm(0xF8); SPI_WriteData(0x8A);
    SPI_WriteComm(0xAC); SPI_WriteData(0x45);
    SPI_WriteComm(0xA0); SPI_WriteData(0xDD);
    SPI_WriteComm(0xA7); SPI_WriteData(0x47);
    SPI_WriteComm(0xFA); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x04);
    SPI_WriteComm(0x86); SPI_WriteData(0x99); SPI_WriteData(0xa3); SPI_WriteData(0xa3); SPI_WriteData(0x51);
    SPI_WriteComm(0xA3); SPI_WriteData(0xEE);
    SPI_WriteComm(0xFD); SPI_WriteData(0x28); SPI_WriteData(0x28); SPI_WriteData(0x00);
    SPI_WriteComm(0x71); SPI_WriteData(0x48);
    SPI_WriteComm(0x72); SPI_WriteData(0x48);
    SPI_WriteComm(0x73); SPI_WriteData(0x00); SPI_WriteData(0x44);
    SPI_WriteComm(0x97); SPI_WriteData(0xEE);
    SPI_WriteComm(0x83); SPI_WriteData(0x93);
    SPI_WriteComm(0x9A); SPI_WriteData(0x72);
    SPI_WriteComm(0x9B); SPI_WriteData(0x5a);
    SPI_WriteComm(0x82); SPI_WriteData(0x2c); SPI_WriteData(0x2c);
    SPI_WriteComm(0xB1); SPI_WriteData(0x10);
    
    uint8_t d_6d[] = {0x00, 0x1F, 0x19, 0x1A, 0x10, 0x0e, 0x0c, 0x0a, 0x02, 0x07, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x08, 0x01, 0x09, 0x0b, 0x0D, 0x0F, 0x1a, 0x19, 0x1f, 0x00};
    SPI_WriteComm(0x6D); for(int i=0; i<32; i++) SPI_WriteData(d_6d[i]);
    
    uint8_t d_64[] = {0x38, 0x05, 0x01, 0xdb, 0x03, 0x03, 0x38, 0x04, 0x01, 0xdc, 0x03, 0x03, 0x7A, 0x7A, 0x7A, 0x7A};
    SPI_WriteComm(0x64); for(int i=0; i<16; i++) SPI_WriteData(d_64[i]);
    
    uint8_t d_65[] = {0x38, 0x03, 0x01, 0xdd, 0x03, 0x03, 0x38, 0x02, 0x01, 0xde, 0x03, 0x03, 0x7A, 0x7A, 0x7A, 0x7A};
    SPI_WriteComm(0x65); for(int i=0; i<16; i++) SPI_WriteData(d_65[i]);
    
    uint8_t d_66[] = {0x38, 0x01, 0x01, 0xdf, 0x03, 0x03, 0x38, 0x00, 0x01, 0xe0, 0x03, 0x03, 0x7A, 0x7A, 0x7A, 0x7A};
    SPI_WriteComm(0x66); for(int i=0; i<16; i++) SPI_WriteData(d_66[i]);
    
    uint8_t d_67[] = {0x30, 0x01, 0x01, 0xe1, 0x03, 0x03, 0x30, 0x02, 0x01, 0xe2, 0x03, 0x03, 0x7A, 0x7A, 0x7A, 0x7A};
    SPI_WriteComm(0x67); for(int i=0; i<16; i++) SPI_WriteData(d_67[i]);
    
    uint8_t d_68[] = {0x00, 0x08, 0x15, 0x08, 0x15, 0x7A, 0x7A, 0x08, 0x15, 0x08, 0x15, 0x7A, 0x7A};
    SPI_WriteComm(0x68); for(int i=0; i<13; i++) SPI_WriteData(d_68[i]);
    
    uint8_t d_60[] = {0x38, 0x08, 0x7A, 0x7A, 0x38, 0x09, 0x7A, 0x7A};
    SPI_WriteComm(0x60); for(int i=0; i<8; i++) SPI_WriteData(d_60[i]);
    
    uint8_t d_63[] = {0x31, 0xe4, 0x7A, 0x7A, 0x31, 0xe5, 0x7A, 0x7A};
    SPI_WriteComm(0x63); for(int i=0; i<8; i++) SPI_WriteData(d_63[i]);
    
    uint8_t d_69[] = {0x04, 0x22, 0x14, 0x22, 0x14, 0x22, 0x08};
    SPI_WriteComm(0x69); for(int i=0; i<7; i++) SPI_WriteData(d_69[i]);
    
    SPI_WriteComm(0x6B); SPI_WriteData(0x07);
    SPI_WriteComm(0x7A); SPI_WriteData(0x08); SPI_WriteData(0x13);
    SPI_WriteComm(0x7B); SPI_WriteData(0x08); SPI_WriteData(0x13);

    uint8_t d_data[] = {0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xE0, 0x03, 0xea, 0x03, 0xFa, 0x03, 0xFF};
    for (uint8_t d_cmd = 0xD1; d_cmd <= 0xD6; d_cmd++) {
        SPI_WriteComm(d_cmd);
        for (int i = 0; i < 52; i++) SPI_WriteData(d_data[i]);
    }

    SPI_WriteComm(0x3a); SPI_WriteData(0x66);
    SPI_WriteComm(0x11); 
    vTaskDelay(pdMS_TO_TICKS(200));
    SPI_WriteComm(0x29); 
}

static void panelLan_rgb_spi_init() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = 48;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = 45;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 10 * 1024;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = SPI_MASTER_FREQ_10M;
    
    // THE FIX: -1 prevents ESP-IDF from hijacking GPIO 38 and corrupting your PSRAM
    devcfg.spics_io_num = -1; 
    devcfg.queue_size = 7;

    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &g_screen_spi));
    rgb_driver_init();
    
    spi_bus_remove_device(g_screen_spi);
    spi_bus_free(SPI2_HOST);
}

// ========================================================
// 2. PANELLAN EXACT OVERRIDE (From bc02.cpp)
// ========================================================
class Panel_BC02_Spec: public lgfx::v1::Panel_RGB {
public:
    bool init(bool use_reset) override {
        ESP_LOGI(TAG, "Triggering LCD_DISP_EN (GPIO 41)...");
        gpio_set_direction((gpio_num_t)41, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)41, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        ESP_LOGI(TAG, "Running PANLEE SPI Init...");
        panelLan_rgb_spi_init();

        ESP_LOGI(TAG, "Booting Panel_RGB...");
        if (!Panel_RGB::init(false)) {
            return false;
        }
        return true;
    }
};

class LGFX : public lgfx::LGFX_Device {
    Panel_BC02_Spec    _panel_instance;
    lgfx::Bus_RGB      _bus_instance;
    lgfx::Light_PWM    _light_instance;
    lgfx::Touch_FT5x06 _touch_instance;

public:
    LGFX(void) {
        auto cfg = _bus_instance.config();
        cfg.panel = &_panel_instance;
        cfg.pin_d0  = 45; cfg.pin_d1  = 48; cfg.pin_d2  = 47; cfg.pin_d3  = 0;
        cfg.pin_d4  = 21; cfg.pin_d5  = 14; cfg.pin_d6  = 13; cfg.pin_d7  = 12;
        cfg.pin_d8  = 11; cfg.pin_d9  = 16; cfg.pin_d10 = 17; cfg.pin_d11 = 18;
        cfg.pin_d12 = 8;  cfg.pin_d13 = 3;  cfg.pin_d14 = 46; cfg.pin_d15 = 10;
        
        cfg.pin_pclk    = 39;
        cfg.pin_vsync   = 41;
        cfg.pin_hsync   = 42;
        cfg.pin_henable = 40;

        cfg.freq_write  = 15000000;

        cfg.hsync_polarity    = 1;
        cfg.hsync_front_porch = 8;
        cfg.hsync_pulse_width = 10;
        cfg.hsync_back_porch  = 40;
        cfg.vsync_polarity    = 1;
        cfg.vsync_front_porch = 8;
        cfg.vsync_pulse_width = 10;
        cfg.vsync_back_porch  = 40;
        cfg.pclk_active_neg   = 0;

        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);

        auto pcfg = _panel_instance.config();
        pcfg.memory_width     = 480;
        pcfg.panel_width      = 480;
        pcfg.memory_height    = 480;
        pcfg.panel_height     = 480;
        pcfg.offset_x         = 0;
        pcfg.offset_y         = 0;
        _panel_instance.config(pcfg);

        auto tcfg = _touch_instance.config();
        tcfg.x_min      = 0;
        tcfg.x_max      = 480;
        tcfg.y_min      = 0;
        tcfg.y_max      = 480;
        tcfg.bus_shared = true;
        tcfg.offset_rotation = 0;
        
        tcfg.i2c_port   = 0; 
        tcfg.pin_int    = 4;
        tcfg.pin_sda    = 15;
        tcfg.pin_scl    = 6;
        tcfg.pin_rst    = -1;
        tcfg.freq       = 400000;
        _touch_instance.config(tcfg);
        _panel_instance.setTouch(&_touch_instance);

        auto lcfg = _light_instance.config();
        lcfg.pin_bl = 5;
        lcfg.invert = false;
        lcfg.freq = 500;
        lcfg.pwm_channel = 7;
        _light_instance.config(lcfg);
        _panel_instance.setLight(&_light_instance);

        setPanel(&_panel_instance);
    }
};

LGFX lcd;

// ========================================================
// 3. LVGL UI & MAIN TASK
// ========================================================
static lv_color_t *buf; 
static lv_disp_draw_buf_t draw_buf;

static void lv_tick_task(void *arg) { lv_tick_inc(2); }

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    lcd.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint16_t touchX, touchY;
    bool touched = lcd.getTouch(&touchX, &touchY);
    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
    }
}

static void btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED) {
        static uint8_t cnt = 0;
        cnt++;
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "Tapped: %d", cnt);
    }
}

void build_test_ui() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x2E3440), LV_PART_MAIN); 

    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Smart Doorbell V3 - PSRAM Fixed");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t * btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Tap Me!");
    lv_obj_center(btn_label);
}

void guiTask(void *pvParameter) {
    lv_init();

    size_t buffer_size = 480 * 40 * sizeof(lv_color_t);
    buf = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 480 * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 480;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    build_test_ui();

    esp_timer_create_args_t periodic_timer_args = {};
    periodic_timer_args.callback = &lv_tick_task;
    periodic_timer_args.name = "periodic_gui";
    
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 2 * 1000));

    ESP_LOGI(TAG, "LVGL Task Running on Core 0");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10)); 
        lv_timer_handler(); 
    }
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "Booting Smart Doorbell V3...");

    lcd.init();
    lcd.setBrightness(128);

    xTaskCreatePinnedToCore(guiTask, "guiTask", 1024 * 8, NULL, 5, NULL, 0);
}