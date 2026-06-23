#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"

// Native ESP-LCD Framework
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

// Official Espressif Hardware Drivers
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lcd_panel_io.h"

#include "lvgl.h"

static const char *TAG = "S3_MAGIC_LCD";

// --- RGB PINS ---
#define LCD_PIN_HSYNC    42
#define LCD_PIN_VSYNC    41
#define LCD_PIN_DE       40
#define LCD_PIN_PCLK     39
#define LCD_PIN_DATA0    45
#define LCD_PIN_DATA1    48
#define LCD_PIN_DATA2    47
#define LCD_PIN_DATA3    0
#define LCD_PIN_DATA4    21
#define LCD_PIN_DATA5    14
#define LCD_PIN_DATA6    13
#define LCD_PIN_DATA7    12
#define LCD_PIN_DATA8    11
#define LCD_PIN_DATA9    16
#define LCD_PIN_DATA10   17
#define LCD_PIN_DATA11   18
#define LCD_PIN_DATA12   8
#define LCD_PIN_DATA13   3
#define LCD_PIN_DATA14   46
#define LCD_PIN_DATA15   10

// --- SPI PINS (Shared with RGB) ---
#define LCD_PIN_CS       38
#define LCD_PIN_SCLK     45 
#define LCD_PIN_MOSI     48 

// --- TOUCH PINS ---
#define TOUCH_PIN_SDA    15
#define TOUCH_PIN_SCL    6
#define TOUCH_PIN_INT    4
#define TOUCH_I2C_PORT   I2C_NUM_0

// --- BACKLIGHT ---
#define LCD_PIN_BL       5

static const uint32_t screenWidth  = 480;
static const uint32_t screenHeight = 480;
static lv_disp_draw_buf_t draw_buf;

// ========================================================
// 1. THE PANLEE TROJAN HORSE (PROPRIETARY SPI WAKEUP)
// ========================================================
static spi_device_handle_t g_screen_spi;

static void lcd_cmd(spi_device_handle_t spi, const uint16_t data) {
    uint16_t tmp_cmd = (data | 0x0000);
    spi_transaction_ext_t trans = {};
    trans.base.flags = SPI_TRANS_VARIABLE_CMD;
    trans.base.cmd = tmp_cmd;
    trans.command_bits = 9; // 9-Bit SPI format
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

static void panlee_proprietary_magic_init(void) {
    ESP_LOGW(TAG, "Executing proprietary PANLEE Hex Sequence...");
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

    // Apply the 52-Byte D-commands
    for (uint8_t d_cmd = 0xD1; d_cmd <= 0xD6; d_cmd++) {
        SPI_WriteComm(d_cmd);
        uint8_t d_data[] = {0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xE0, 0x03, 0xea, 0x03, 0xFa, 0x03, 0xFF};
        for (int i = 0; i < 52; i++) SPI_WriteData(d_data[i]);
    }

    SPI_WriteComm(0x3a); SPI_WriteData(0x66);
    SPI_WriteComm(0x11); // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(200));
    SPI_WriteComm(0x29); // Display On
    ESP_LOGI(TAG, "Proprietary Wakeup Complete.");
}

static void trigger_hardware_bootloader() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = LCD_PIN_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = LCD_PIN_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 10 * 1024;
    
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = SPI_MASTER_FREQ_10M;
    devcfg.spics_io_num = LCD_PIN_CS;
    devcfg.queue_size = 7;

    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &g_screen_spi));
    
    panlee_proprietary_magic_init();

    // DESTROY the SPI interface so RGB DMA can safely claim pins 45 and 48
    spi_bus_remove_device(g_screen_spi);
    spi_bus_free(SPI2_HOST);
}

// ========================================================
// 2. LVGL BRIDGING FUNCTIONS
// ========================================================
static void lv_tick_task(void *arg) { lv_tick_inc(2); }

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)disp->user_data;
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)indev_driver->user_data;
    esp_lcd_touch_read_data(tp);

    esp_lcd_touch_point_data_t tp_data[1]; 
    uint8_t touch_cnt = 0;
    esp_err_t err = esp_lcd_touch_get_data(tp, tp_data, &touch_cnt, 1);

    if (err == ESP_OK && touch_cnt > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = tp_data[0].x;
        data->point.y = tp_data[0].y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ========================================================
// 3. UI GENERATION
// ========================================================
static void btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED) {
        static uint8_t cnt = 0;
        cnt++;
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "NATIVE Tapped: %d", cnt);
        ESP_LOGI(TAG, "Touch recognized! Count: %d", cnt);
    }
}

void build_test_ui() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x004488), LV_PART_MAIN); 

    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Smart Doorbell V3 - NATIVE LCD");
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
    ESP_LOGI(TAG, "--- STAGE 1: SPI Wakeup ---");
    trigger_hardware_bootloader();

    ESP_LOGI(TAG, "--- STAGE 2: Native RGB Matrix Boot ---");
    esp_lcd_rgb_panel_config_t rgb_config = {};
    rgb_config.clk_src = LCD_CLK_SRC_DEFAULT;
    
    // Exactly matched to PanelLan's internal timings
    rgb_config.timings.pclk_hz = 15 * 1000 * 1000;
    rgb_config.timings.h_res = screenWidth;
    rgb_config.timings.v_res = screenHeight;
    rgb_config.timings.hsync_pulse_width = 10;
    rgb_config.timings.hsync_back_porch = 40;
    rgb_config.timings.hsync_front_porch = 8;
    rgb_config.timings.vsync_pulse_width = 10;
    rgb_config.timings.vsync_back_porch = 40;
    rgb_config.timings.vsync_front_porch = 8;
    
    rgb_config.data_width = 16;
    rgb_config.psram_trans_align = 64;
    rgb_config.bounce_buffer_size_px = 10 * screenWidth;
    
    rgb_config.hsync_gpio_num = LCD_PIN_HSYNC;
    rgb_config.vsync_gpio_num = LCD_PIN_VSYNC;
    rgb_config.de_gpio_num = LCD_PIN_DE;
    rgb_config.pclk_gpio_num = LCD_PIN_PCLK;
    
    rgb_config.data_gpio_nums[0] = LCD_PIN_DATA0;  rgb_config.data_gpio_nums[1] = LCD_PIN_DATA1;
    rgb_config.data_gpio_nums[2] = LCD_PIN_DATA2;  rgb_config.data_gpio_nums[3] = LCD_PIN_DATA3;
    rgb_config.data_gpio_nums[4] = LCD_PIN_DATA4;  rgb_config.data_gpio_nums[5] = LCD_PIN_DATA5;
    rgb_config.data_gpio_nums[6] = LCD_PIN_DATA6;  rgb_config.data_gpio_nums[7] = LCD_PIN_DATA7;
    rgb_config.data_gpio_nums[8] = LCD_PIN_DATA8;  rgb_config.data_gpio_nums[9] = LCD_PIN_DATA9;
    rgb_config.data_gpio_nums[10] = LCD_PIN_DATA10; rgb_config.data_gpio_nums[11] = LCD_PIN_DATA11;
    rgb_config.data_gpio_nums[12] = LCD_PIN_DATA12; rgb_config.data_gpio_nums[13] = LCD_PIN_DATA13;
    rgb_config.data_gpio_nums[14] = LCD_PIN_DATA14; rgb_config.data_gpio_nums[15] = LCD_PIN_DATA15;
    rgb_config.flags.fb_in_psram = 1;

    esp_lcd_panel_handle_t rgb_panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&rgb_config, &rgb_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(rgb_panel_handle));

    ESP_LOGI(TAG, "--- STAGE 3: Touch Controller Boot ---");
    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = TOUCH_PIN_SDA;
    i2c_conf.scl_io_num = TOUCH_PIN_SCL;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = 400000;
    
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_PORT, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_PORT, i2c_conf.mode, 0, 0, 0));

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {};
    tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    tp_io_config.control_phase_bytes = 1;
    tp_io_config.lcd_cmd_bits = 8;
    tp_io_config.lcd_param_bits = 8;
    tp_io_config.flags.disable_control_phase = 1;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v1((i2c_port_t)TOUCH_I2C_PORT, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = screenWidth;
    tp_cfg.y_max = screenHeight;
    tp_cfg.rst_gpio_num = GPIO_NUM_NC;
    tp_cfg.int_gpio_num = (gpio_num_t)TOUCH_PIN_INT;
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;
    
    esp_lcd_touch_handle_t tp_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp_handle));

    ESP_LOGI(TAG, "--- STAGE 4: LVGL Mounting ---");
    lv_init();

    size_t draw_buffer_size = screenWidth * 40;
    void *buf1 = heap_caps_malloc(draw_buffer_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, draw_buffer_size);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = rgb_panel_handle; 
    disp_drv.full_refresh = 0; 
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    indev_drv.user_data = tp_handle;
    lv_indev_drv_register(&indev_drv);

    build_test_ui();

    esp_timer_create_args_t periodic_timer_args = {};
    periodic_timer_args.callback = &lv_tick_task;
    periodic_timer_args.name = "periodic_gui";
    
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 2 * 1000));

    // Power the Backlight
    gpio_set_direction((gpio_num_t)LCD_PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)LCD_PIN_BL, 1);

    ESP_LOGI(TAG, "ALL SYSTEMS GO. Native LVGL Task Running on Core 0");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "Booting Smart Doorbell V3...");

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