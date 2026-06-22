#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

// LovyanGFX and LVGL
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "lvgl.h"

static const char *TAG = "S3_DISPLAY";

// ========================================================
// 1. LOVYANGFX HARDWARE CONFIGURATION
// ========================================================
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9503 _panel_instance;
    lgfx::Bus_RGB      _bus_instance;
    lgfx::Touch_FT5x06 _touch_instance;

public:
    LGFX(void) {
        // A. Configure the RGB Bus (Data lines)
        auto cfg = _bus_instance.config();
        cfg.panel = &_panel_instance;
        cfg.pin_d0  = 45; cfg.pin_d1  = 48; cfg.pin_d2  = 47; cfg.pin_d3  = 0;
        cfg.pin_d4  = 21; cfg.pin_d5  = 14; cfg.pin_d6  = 13; cfg.pin_d7  = 12;
        cfg.pin_d8  = 11; cfg.pin_d9  = 16; cfg.pin_d10 = 17; cfg.pin_d11 = 18;
        cfg.pin_d12 = 8;  cfg.pin_d13 = 3;  cfg.pin_d14 = 46; cfg.pin_d15 = 10;
        
        cfg.pin_henable = 40; // DE
        cfg.pin_vsync   = 41; // VS
        cfg.pin_hsync   = 42; // HS
        cfg.pin_pclk    = 39; // PCLK
        
        cfg.freq_write  = 14000000;
        cfg.hsync_polarity    = 0;
        cfg.hsync_front_porch = 8;
        cfg.hsync_pulse_width = 4;
        cfg.hsync_back_porch  = 8;
        cfg.vsync_polarity    = 0;
        cfg.vsync_front_porch = 8;
        cfg.vsync_pulse_width = 4;
        cfg.vsync_back_porch  = 8;
        cfg.pclk_idle_high    = 1;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);

        // B. Configure the Panel (SPI Init shared with RGB pins)
        auto pcfg = _panel_instance.config();
        pcfg.pin_cs    = 38;
        pcfg.pin_rst   = -1; // RC reset
        pcfg.pin_mosi  = 48; // Shared with D1
        pcfg.pin_sclk  = 45; // Shared with D0
        
        pcfg.panel_width      = 480;
        pcfg.panel_height     = 480;
        pcfg.offset_x         = 0;
        pcfg.offset_y         = 0;
        pcfg.memory_width     = 480;
        pcfg.memory_height    = 480;
        pcfg.bus_shared       = true; // Critical for shared SPI/RGB pins
        
        _panel_instance.config(pcfg);

        // C. Configure the Touch Controller (I2C)
        auto tcfg = _touch_instance.config();
        tcfg.x_min      = 0;
        tcfg.x_max      = 479;
        tcfg.y_min      = 0;
        tcfg.y_max      = 479;
        tcfg.pin_int    = 4;
        tcfg.bus_shared = false;
        tcfg.offset_rotation = 0;
        
        // I2C Pins for FT6336U
        tcfg.i2c_port   = 0;
        tcfg.i2c_addr   = 0x38;
        tcfg.pin_sda    = 15;
        tcfg.pin_scl    = 6;
        tcfg.freq       = 400000;
        
        _touch_instance.config(tcfg);
        _panel_instance.setTouch(&_touch_instance);
        
        // Backlight
        auto blcfg = _panel_instance.config_detail();
        blcfg.pin_backlight = 5;
        _panel_instance.config_detail(blcfg);

        setPanel(&_panel_instance);
    }
};

LGFX lcd; // Instantiate the global display object

// ========================================================
// 2. LVGL BRIDGING FUNCTIONS
// ========================================================
static const uint32_t screenWidth  = 480;
static const uint32_t screenHeight = 480;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf; // Render buffer mapped in PSRAM

// Push the memory buffer to the physical screen
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    lcd.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}

// Read the physical touch coordinates into LVGL
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

// ========================================================
// 3. UI GENERATION (TESTING VISUALS & TOUCH)
// ========================================================
static void btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED) {
        static uint8_t cnt = 0;
        cnt++;
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "Tapped: %d times", cnt);
        ESP_LOGI(TAG, "Touch recognized!");
    }
}

void build_test_ui() {
    // Set a dark background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1a1a1a), LV_PART_MAIN);

    // Add a title text
    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Smart Doorbell V3 - Core 0");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 50);

    // Add an interactive button
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
    // Initialize LVGL
    lv_init();

    // Allocate memory for the LVGL buffer in PSRAM (crucial for RGB displays)
    size_t buffer_size = screenWidth * 40 * sizeof(lv_color_t);
    buf = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 40);

    // Initialize the display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Initialize the touch driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Build the UI
    build_test_ui();

    ESP_LOGI(TAG, "LVGL Task Running on Core 0");

    // Infinite FreeRTOS loop to keep UI ticking
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield to RTOS
        lv_timer_handler(); // Tell LVGL to do its work
    }
}

// ========================================================
// 5. MAIN ENTRY POINT
// ========================================================
extern "C" void app_main() {
    ESP_LOGI(TAG, "Booting Smart Doorbell V3...");

    // Boot the hardware screen
    lcd.init();
    lcd.initDMA();
    lcd.setBrightness(128); // 50% brightness

    // Pin the GUI to Core 0. (Core 1 is reserved for esp-dl later)
    xTaskCreatePinnedToCore(
        guiTask,     // Task function
        "guiTask",   // Task name
        1024 * 8,    // Stack size (8KB)
        NULL,        // Parameters
        5,           // Priority
        NULL,        // Task handle
        0            // Core ID
    );
}