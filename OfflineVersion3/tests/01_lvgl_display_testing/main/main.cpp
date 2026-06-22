#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

// 1. Pure ESP-IDF Native Includes (No Arduino)
#include "boards.h" 
#include "lvgl.h"

static const char *TAG = "S3_NATIVE_CORE";

// 2. Instantiate the exact manufacturer board
PanelLan tft(BOARD_BC02);

// 3. LVGL Buffers
static const uint32_t screenWidth  = 480;
static const uint32_t screenHeight = 480;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;

static void lv_tick_task(void *arg) { lv_tick_inc(2); }

// 4. LVGL Hardware Bridging
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
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

// 5. Minimal UI Test
void build_test_ui() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x004488), LV_PART_MAIN); 

    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Smart Doorbell V3 - NATIVE ESP-IDF");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 50);
}

// 6. FreeRTOS GUI Task
void guiTask(void *pvParameter) {
    ESP_LOGI(TAG, "Mounting LVGL...");
    lv_init();

    size_t buffer_size = screenWidth * 40 * sizeof(lv_color_t);
    // CRITICAL: Force the LVGL draw buffer into PSRAM to save internal memory for the ML models
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

// 7. Pure Native ESP-IDF Entry Point
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Booting Smart Doorbell V3...");

    // With the DMA memory expanded in menuconfig, this will successfully allocate the RGB buffers.
    if (!tft.init()) {
        ESP_LOGE(TAG, "FATAL: PanelLan initialization failed! Check DMA Memory limits.");
        return; // Halt before we null-pointer crash
    }
    
    tft.setBrightness(128);

    // Pin the UI to Core 0. Your ML partner will use Core 1 for Face Recognition.
    xTaskCreatePinnedToCore(guiTask, "guiTask", 1024 * 8, NULL, 5, NULL, 0);
}