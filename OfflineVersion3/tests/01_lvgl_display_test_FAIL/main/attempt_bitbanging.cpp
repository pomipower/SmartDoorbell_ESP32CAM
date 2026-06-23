#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h" 

// 1. LVGL must be included BEFORE LovyanGFX
#include "lvgl.h"

// 2. Include LovyanGFX Base + ESP32-S3 specific hardware drivers
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>

static const char *TAG = "S3_PANELLAN_CLONE";

// ========================================================
// 1. THE BIT-BANGED PANELLAN BOOTLOADER 
// ========================================================
#define LCD_SPI_MOSI 48
#define LCD_SPI_CLK  45
#define LCD_SPI_CS   38

static void bb_spi_init() {
    gpio_set_direction((gpio_num_t)LCD_SPI_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)LCD_SPI_CLK, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)LCD_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)LCD_SPI_CS, 1);
    gpio_set_level((gpio_num_t)LCD_SPI_CLK, 0);
}

// 100% foolproof 9-bit SPI transmission (Bypasses ESP-IDF driver entirely)
static void bb_spi_send(uint8_t data, bool is_data) {
    gpio_set_level((gpio_num_t)LCD_SPI_CS, 0);
    
    // 9th bit: D/C bit (0 for Command, 1 for Data)
    gpio_set_level((gpio_num_t)LCD_SPI_MOSI, is_data ? 1 : 0);
    gpio_set_level((gpio_num_t)LCD_SPI_CLK, 1);
    esp_rom_delay_us(1); 
    gpio_set_level((gpio_num_t)LCD_SPI_CLK, 0);
    esp_rom_delay_us(1);

    // Send 8 bits of payload, MSB first
    for (int i = 7; i >= 0; i--) {
        gpio_set_level((gpio_num_t)LCD_SPI_MOSI, (data >> i) & 0x01);
        gpio_set_level((gpio_num_t)LCD_SPI_CLK, 1);
        esp_rom_delay_us(1);
        gpio_set_level((gpio_num_t)LCD_SPI_CLK, 0);
        esp_rom_delay_us(1);
    }
    gpio_set_level((gpio_num_t)LCD_SPI_CS, 1);
}

static void SPI_WriteComm(uint8_t cmd)  { bb_spi_send(cmd, false); }
static void SPI_WriteData(uint8_t data) { bb_spi_send(data, true); }

static void panlee_proprietary_magic_init(void) {
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
    SPI_WriteComm(0x6D); SPI_WriteData(0x00); SPI_WriteData(0x1F); SPI_WriteData(0x19); SPI_WriteData(0x1A); SPI_WriteData(0x10); SPI_WriteData(0x0e); SPI_WriteData(0x0c); SPI_WriteData(0x0a); SPI_WriteData(0x02); SPI_WriteData(0x07); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x1E); SPI_WriteData(0x08); SPI_WriteData(0x01); SPI_WriteData(0x09); SPI_WriteData(0x0b); SPI_WriteData(0x0D); SPI_WriteData(0x0F); SPI_WriteData(0x1a); SPI_WriteData(0x19); SPI_WriteData(0x1f); SPI_WriteData(0x00);
    SPI_WriteComm(0x64); SPI_WriteData(0x38); SPI_WriteData(0x05); SPI_WriteData(0x01); SPI_WriteData(0xdb); SPI_WriteData(0x03); SPI_WriteData(0x03); SPI_WriteData(0x38); SPI_WriteData(0x04); SPI_WriteData(0x01); SPI_WriteData(0xdc); SPI_WriteData(0x03); SPI_WriteData(0x03); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x7A);
    SPI_WriteComm(0x65); SPI_WriteData(0x38); SPI_WriteData(0x03); SPI_WriteData(0x01); SPI_WriteData(0xdd); SPI_WriteData(0x03); SPI_WriteData(0x03); SPI_WriteData(0x38); SPI_WriteData(0x02); SPI_WriteData(0x01); SPI_WriteData(0xde); SPI_WriteData(0x03); SPI_WriteData(0x03); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x7A);
    SPI_WriteComm(0x66); SPI_WriteData(0x38); SPI_WriteData(0x01); SPI_WriteData(0x01); SPI_WriteData(0xdf); SPI_WriteData(0x03); SPI_WriteData(0x03); SPI_WriteData(0x38); SPI_WriteData(0x00); SPI_WriteData(0x01); SPI_WriteData(0xe0); SPI_WriteData(0x03); SPI_WriteData(0x03); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x7A);
    SPI_WriteComm(0x67); SPI_WriteData(0x30); SPI_WriteData(0x01); SPI_WriteData(0x01); SPI_WriteData(0xe1); SPI_WriteData(0x03); SPI_WriteData(0x03); SPI_WriteData(0x30); SPI_WriteData(0x02); SPI_WriteData(0x01); SPI_WriteData(0xe2); SPI_WriteData(0x03); SPI_WriteData(0x03); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x7A);
    SPI_WriteComm(0x68); SPI_WriteData(0x00); SPI_WriteData(0x08); SPI_WriteData(0x15); SPI_WriteData(0x08); SPI_WriteData(0x15); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x08); SPI_WriteData(0x15); SPI_WriteData(0x08); SPI_WriteData(0x15); SPI_WriteData(0x7A); SPI_WriteData(0x7A);
    SPI_WriteComm(0x60); SPI_WriteData(0x38); SPI_WriteData(0x08); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x38); SPI_WriteData(0x09); SPI_WriteData(0x7A); SPI_WriteData(0x7A);
    SPI_WriteComm(0x63); SPI_WriteData(0x31); SPI_WriteData(0xe4); SPI_WriteData(0x7A); SPI_WriteData(0x7A); SPI_WriteData(0x31); SPI_WriteData(0xe5); SPI_WriteData(0x7A); SPI_WriteData(0x7A);
    SPI_WriteComm(0x69); SPI_WriteData(0x04); SPI_WriteData(0x22); SPI_WriteData(0x14); SPI_WriteData(0x22); SPI_WriteData(0x14); SPI_WriteData(0x22); SPI_WriteData(0x08);
    SPI_WriteComm(0x6B); SPI_WriteData(0x07);
    SPI_WriteComm(0x7A); SPI_WriteData(0x08); SPI_WriteData(0x13);
    SPI_WriteComm(0x7B); SPI_WriteData(0x08); SPI_WriteData(0x13);

    for (uint8_t d_cmd = 0xD1; d_cmd <= 0xD6; d_cmd++) {
        SPI_WriteComm(d_cmd);
        uint8_t d_data[] = {0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xE0, 0x03, 0xea, 0x03, 0xFa, 0x03, 0xFF};
        for (int i = 0; i < 52; i++) SPI_WriteData(d_data[i]);
    }

    SPI_WriteComm(0x3a); SPI_WriteData(0x66);
    SPI_WriteComm(0x11); // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(200));
    SPI_WriteComm(0x29); // Display On
}

// ========================================================
// 2. THE CUSTOM PANEL_RGB OVERRIDE (Replicating bc02.cpp)
// ========================================================
class Panel_BC02_Spec : public lgfx::v1::Panel_RGB {
public:
    bool init(bool use_reset) override {
        ESP_LOGI(TAG, "1. Hardware Reset (LCD_DISP_EN_GPIO 41)...");
        gpio_set_direction((gpio_num_t)41, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)41, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        ESP_LOGI(TAG, "2. Bit-Banging Proprietary PANLEE Sequence...");
        bb_spi_init();
        panlee_proprietary_magic_init();

        // Sever the bit-bang lock so RGB DMA can safely claim pins 45 and 48
        gpio_reset_pin((gpio_num_t)LCD_SPI_MOSI);
        gpio_reset_pin((gpio_num_t)LCD_SPI_CLK);
        gpio_reset_pin((gpio_num_t)LCD_SPI_CS);

        ESP_LOGI(TAG, "3. Booting Native Panel_RGB...");
        if (!Panel_RGB::init(false)) {
            return false;
        }
        return true;
    }
};

class LGFX : public lgfx::LGFX_Device {
    Panel_BC02_Spec    _panel_instance; // Bind our custom bootloader class
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
        
        cfg.pin_henable = 40; 
        cfg.pin_vsync   = 41; 
        cfg.pin_hsync   = 42; 
        cfg.pin_pclk    = 39; 
        
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
        
        tcfg.i2c_port   = 1; // FIXED: Restored exactly to bc02.cpp mapping
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
// 3. LVGL BRIDGING & UI
// ========================================================
static const uint32_t screenWidth  = 480;
static const uint32_t screenHeight = 480;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf; 

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
        lv_label_set_text_fmt(label, "PANELLAN Tapped: %d", cnt);
        ESP_LOGI(TAG, "Touch recognized! Count: %d", cnt);
    }
}

void build_test_ui() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x008844), LV_PART_MAIN); // Green background

    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Smart Doorbell V3 - BAREMETAL RGB");
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

// ========================================================
// 4. FREERTOS TASK
// ========================================================
void guiTask(void *pvParameter) {
    lv_init();

    size_t buffer_size = screenWidth * 40 * sizeof(lv_color_t);
    buf = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    build_test_ui();

    ESP_LOGI(TAG, "LVGL Task Running on Core 0");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10)); 
        lv_timer_handler(); 
    }
}

// ========================================================
// 5. MAIN ENTRY POINT
// ========================================================
extern "C" void app_main() {
    ESP_LOGI(TAG, "Booting Smart Doorbell V3...");

    // The entire SPI sequence and RGB matrix boot is now safely encapsulated inside this single init call.
    lcd.init();
    lcd.setBrightness(128);

    xTaskCreatePinnedToCore(
        guiTask,     
        "guiTask",   
        1024 * 8,    
        NULL,        
        5,           
        NULL,        
        0            
    );
}