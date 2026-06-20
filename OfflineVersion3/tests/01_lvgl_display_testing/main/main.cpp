#include "Arduino.h"
#include "boards.h" 
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

// 1. Hardware Initialization
PanelLan tft(BOARD_BC02);

// 2. LVGL Buffers
static const uint32_t screenWidth  = 480;
static const uint32_t screenHeight = 480;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;

static void lv_tick_task(void *arg) { lv_tick_inc(2); }

// 3. LVGL Bridging
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

// 4. Test UI
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
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x005500), LV_PART_MAIN); 

    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Smart Doorbell V3 - SYSTEM ONLINE");
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

// 5. FreeRTOS Task
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

    esp_timer_create_args_t periodic_timer_args = {};
    periodic_timer_args.callback = &lv_tick_task;
    periodic_timer_args.name = "periodic_gui";
    
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 2 * 1000));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10)); 
        lv_timer_handler(); 
    }
}

// 6. Native ESP-IDF Entry Point
extern "C" void app_main() {
    // Crucial: Boot the Arduino framework underlying HAL
    initArduino();

    // Now properly compile and execute the manufacturer's logic
    tft.init();
    tft.setBrightness(128);

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