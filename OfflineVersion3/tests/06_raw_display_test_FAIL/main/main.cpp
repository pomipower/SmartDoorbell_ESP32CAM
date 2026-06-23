#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_heap_caps.h"

static const char *TAG = "PANEL_LAN_PORT";

// ========================================================
// 1. PIN DEFINITIONS (From bc02_pin.h)
// ========================================================
#define LCD_SPI_CS      GPIO_NUM_38
#define LCD_SPI_CLK     GPIO_NUM_45 
#define LCD_SPI_MOSI    GPIO_NUM_48 

#define LCD_PCLK        GPIO_NUM_39
#define LCD_DE          GPIO_NUM_40
#define LCD_VSYNC       GPIO_NUM_41 // DISPLAY ENABLE
#define LCD_HSYNC       GPIO_NUM_42

#define LCD_D0          GPIO_NUM_45 // B0
#define LCD_D1          GPIO_NUM_48 // B1
#define LCD_D2          GPIO_NUM_47 // B2
#define LCD_D3          GPIO_NUM_0  // B3
#define LCD_D4          GPIO_NUM_21 // B4
#define LCD_D5          GPIO_NUM_14 // G0
#define LCD_D6          GPIO_NUM_13 // G1
#define LCD_D7          GPIO_NUM_12 // G2
#define LCD_D8          GPIO_NUM_11 // G3
#define LCD_D9          GPIO_NUM_16 // G4
#define LCD_D10         GPIO_NUM_17 // G5
#define LCD_D11         GPIO_NUM_18 // R0
#define LCD_D12         GPIO_NUM_8  // R1
#define LCD_D13         GPIO_NUM_3  // R2
#define LCD_D14         GPIO_NUM_46 // R3
#define LCD_D15         GPIO_NUM_10 // R4

#define LCD_BL          GPIO_NUM_5

// ========================================================
// 2. THE 9-BIT SPI HACK (Matched exactly to Arduino source)
// ========================================================
spi_device_handle_t g_screen_spi;

static void write_cmd(uint16_t cmd) {
    spi_transaction_ext_t trans = {}; // Explicit zero-initialization fixes the warnings
    trans.base.flags = SPI_TRANS_VARIABLE_CMD;
    trans.base.cmd = (cmd | 0x0000);
    trans.base.length = 0; // Explicitly tell the driver there is no data phase
    trans.command_bits = 9;
    ESP_ERROR_CHECK(spi_device_transmit(g_screen_spi, (spi_transaction_t*)&trans));
}

static void write_data(uint16_t data) {
    spi_transaction_ext_t trans = {}; 
    trans.base.flags = SPI_TRANS_VARIABLE_CMD;
    trans.base.cmd = (data | 0x0100);
    trans.base.length = 0; 
    trans.command_bits = 9;
    ESP_ERROR_CHECK(spi_device_transmit(g_screen_spi, (spi_transaction_t*)&trans));
}

// ========================================================
// 3. MAIN SEQUENCE
// ========================================================
extern "C" void app_main() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "STARTING PANEL_LAN HARDWARE PORT");
    ESP_LOGI(TAG, "========================================");

    // --- STEP 1: ENABLE DISPLAY LOGIC ---
    ESP_LOGI(TAG, "[1] Activating Display Enable (GPIO 41)...");
    gpio_set_direction(LCD_VSYNC, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_VSYNC, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // --- STEP 2: HIJACK HARDWARE SPI ---
    ESP_LOGI(TAG, "[2] Initializing 9-Bit Hardware SPI Hack...");
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 10 * 1024,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .mode = 0,                             
        .clock_speed_hz = SPI_MASTER_FREQ_10M, // Restored to 10MHz
        .spics_io_num = LCD_SPI_CS,            
        .queue_size = 7,                       
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &g_screen_spi));

    // --- STEP 3: SEND PROPRIETARY HEX SEQUENCE ---
    ESP_LOGI(TAG, "[3] Injecting GC9503V Hex Sequence...");
    write_cmd(0xF0);
    write_data(0x55);
    write_data(0xAA);
    write_data(0x52);
    write_data(0x08);
    write_data(0x00);

    write_cmd(0xF6);
    write_data(0x5A);
    write_data(0x87);

    write_cmd(0xC1);
    write_data(0x3F);

    write_cmd(0xC2);
    write_data(0x0E);

    write_cmd(0xC6);
    write_data(0xF8);

    write_cmd(0xC9);
    write_data(0x10);

    write_cmd(0xCD);
    write_data(0x25);

    write_cmd(0xF8);
    write_data(0x8A);

    write_cmd(0xAC);
    write_data(0x45);

    write_cmd(0xA0);
    write_data(0xDD);

    write_cmd(0xA7);
    write_data(0x47);

    write_cmd(0xFA);
    write_data(0x00);
    write_data(0x00);
    write_data(0x00);
    write_data(0x04);

    write_cmd(0x86);
    write_data(0x99);
    write_data(0xa3);
    write_data(0xa3);
    write_data(0x51);

    write_cmd(0xA3);
    write_data(0xEE);

    write_cmd(0xFD);
    write_data(0x28);
    write_data(0x28);
    write_data(0x00);

    write_cmd(0x71);
    write_data(0x48);

    write_cmd(0x72);
    write_data(0x48);

    write_cmd(0x73);
    write_data(0x00);
    write_data(0x44);

    write_cmd(0x97);
    write_data(0xEE);

    write_cmd(0x83);
    write_data(0x93);

    write_cmd(0x9A);
    write_data(0x72);

    write_cmd(0x9B);
    write_data(0x5a);

    write_cmd(0x82);
    write_data(0x2c);
    write_data(0x2c);

    write_cmd(0xB1);
    write_data(0x10);

    write_cmd(0x6D);
    write_data(0x00);
    write_data(0x1F);
    write_data(0x19);
    write_data(0x1A);
    write_data(0x10);
    write_data(0x0e);
    write_data(0x0c);
    write_data(0x0a);
    write_data(0x02);
    write_data(0x07);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x1E);
    write_data(0x08);
    write_data(0x01);
    write_data(0x09);
    write_data(0x0b);
    write_data(0x0D);
    write_data(0x0F);
    write_data(0x1a);
    write_data(0x19);
    write_data(0x1f);
    write_data(0x00);

    write_cmd(0x64);
    write_data(0x38);
    write_data(0x05);
    write_data(0x01);
    write_data(0xdb);
    write_data(0x03);
    write_data(0x03);
    write_data(0x38);
    write_data(0x04);
    write_data(0x01);
    write_data(0xdc);
    write_data(0x03);
    write_data(0x03);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x7A);

    write_cmd(0x65);
    write_data(0x38);
    write_data(0x03);
    write_data(0x01);
    write_data(0xdd);
    write_data(0x03);
    write_data(0x03);
    write_data(0x38);
    write_data(0x02);
    write_data(0x01);
    write_data(0xde);
    write_data(0x03);
    write_data(0x03);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x7A);

    write_cmd(0x66);
    write_data(0x38);
    write_data(0x01);
    write_data(0x01);
    write_data(0xdf);
    write_data(0x03);
    write_data(0x03);
    write_data(0x38);
    write_data(0x00);
    write_data(0x01);
    write_data(0xe0);
    write_data(0x03);
    write_data(0x03);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x7A);

    write_cmd(0x67);
    write_data(0x30);
    write_data(0x01);
    write_data(0x01);
    write_data(0xe1);
    write_data(0x03);
    write_data(0x03);
    write_data(0x30);
    write_data(0x02);
    write_data(0x01);
    write_data(0xe2);
    write_data(0x03);
    write_data(0x03);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x7A);

    write_cmd(0x68);
    write_data(0x00);
    write_data(0x08);
    write_data(0x15);
    write_data(0x08);
    write_data(0x15);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x08);
    write_data(0x15);
    write_data(0x08);
    write_data(0x15);
    write_data(0x7A);
    write_data(0x7A);

    write_cmd(0x60);
    write_data(0x38);
    write_data(0x08);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x38);
    write_data(0x09);
    write_data(0x7A);
    write_data(0x7A);

    write_cmd(0x63);
    write_data(0x31);
    write_data(0xe4);
    write_data(0x7A);
    write_data(0x7A);
    write_data(0x31);
    write_data(0xe5);
    write_data(0x7A);
    write_data(0x7A);

    write_cmd(0x69);
    write_data(0x04);
    write_data(0x22);
    write_data(0x14);
    write_data(0x22);
    write_data(0x14);
    write_data(0x22);
    write_data(0x08);

    write_cmd(0x6B);
    write_data(0x07);

    write_cmd(0x7A);
    write_data(0x08);
    write_data(0x13);

    write_cmd(0x7B);
    write_data(0x08);
    write_data(0x13);

    write_cmd(0xD1);
    write_data(0x00);
    write_data(0x00);
    write_data(0x00);
    write_data(0x04);
    write_data(0x00);
    write_data(0x12);
    write_data(0x00);
    write_data(0x18);
    write_data(0x00);
    write_data(0x21);
    write_data(0x00);
    write_data(0x2a);
    write_data(0x00);
    write_data(0x35);
    write_data(0x00);
    write_data(0x47);
    write_data(0x00);
    write_data(0x56);
    write_data(0x00);
    write_data(0x90);
    write_data(0x00);
    write_data(0xe5);
    write_data(0x01);
    write_data(0x68);
    write_data(0x01);
    write_data(0xd5);
    write_data(0x01);
    write_data(0xd7);
    write_data(0x02);
    write_data(0x36);
    write_data(0x02);
    write_data(0xa6);
    write_data(0x02);
    write_data(0xee);
    write_data(0x03);
    write_data(0x48);
    write_data(0x03);
    write_data(0xa0);
    write_data(0x03);
    write_data(0xba);
    write_data(0x03);
    write_data(0xc5);
    write_data(0x03);
    write_data(0xd0);
    write_data(0x03);
    write_data(0xE0);
    write_data(0x03);
    write_data(0xea);
    write_data(0x03);
    write_data(0xFa);
    write_data(0x03);
    write_data(0xFF);

    write_cmd(0xD2);
    write_data(0x00);
    write_data(0x00);
    write_data(0x00);
    write_data(0x04);
    write_data(0x00);
    write_data(0x12);
    write_data(0x00);
    write_data(0x18);
    write_data(0x00);
    write_data(0x21);
    write_data(0x00);
    write_data(0x2a);
    write_data(0x00);
    write_data(0x35);
    write_data(0x00);
    write_data(0x47);
    write_data(0x00);
    write_data(0x56);
    write_data(0x00);
    write_data(0x90);
    write_data(0x00);
    write_data(0xe5);
    write_data(0x01);
    write_data(0x68);
    write_data(0x01);
    write_data(0xd5);
    write_data(0x01);
    write_data(0xd7);
    write_data(0x02);
    write_data(0x36);
    write_data(0x02);
    write_data(0xa6);
    write_data(0x02);
    write_data(0xee);
    write_data(0x03);
    write_data(0x48);
    write_data(0x03);
    write_data(0xa0);
    write_data(0x03);
    write_data(0xba);
    write_data(0x03);
    write_data(0xc5);
    write_data(0x03);
    write_data(0xd0);
    write_data(0x03);
    write_data(0xE0);
    write_data(0x03);
    write_data(0xea);
    write_data(0x03);
    write_data(0xFa);
    write_data(0x03);
    write_data(0xFF);

    write_cmd(0xD3);
    write_data(0x00);
    write_data(0x00);
    write_data(0x00);
    write_data(0x04);
    write_data(0x00);
    write_data(0x12);
    write_data(0x00);
    write_data(0x18);
    write_data(0x00);
    write_data(0x21);
    write_data(0x00);
    write_data(0x2a);
    write_data(0x00);
    write_data(0x35);
    write_data(0x00);
    write_data(0x47);
    write_data(0x00);
    write_data(0x56);
    write_data(0x00);
    write_data(0x90);
    write_data(0x00);
    write_data(0xe5);
    write_data(0x01);
    write_data(0x68);
    write_data(0x01);
    write_data(0xd5);
    write_data(0x01);
    write_data(0xd7);
    write_data(0x02);
    write_data(0x36);
    write_data(0x02);
    write_data(0xa6);
    write_data(0x02);
    write_data(0xee);
    write_data(0x03);
    write_data(0x48);
    write_data(0x03);
    write_data(0xa0);
    write_data(0x03);
    write_data(0xba);
    write_data(0x03);
    write_data(0xc5);
    write_data(0x03);
    write_data(0xd0);
    write_data(0x03);
    write_data(0xE0);
    write_data(0x03);
    write_data(0xea);
    write_data(0x03);
    write_data(0xFa);
    write_data(0x03);
    write_data(0xFF);

    write_cmd(0xD4);
    write_data(0x00);
    write_data(0x00);
    write_data(0x00);
    write_data(0x04);
    write_data(0x00);
    write_data(0x12);
    write_data(0x00);
    write_data(0x18);
    write_data(0x00);
    write_data(0x21);
    write_data(0x00);
    write_data(0x2a);
    write_data(0x00);
    write_data(0x35);
    write_data(0x00);
    write_data(0x47);
    write_data(0x00);
    write_data(0x56);
    write_data(0x00);
    write_data(0x90);
    write_data(0x00);
    write_data(0xe5);
    write_data(0x01);
    write_data(0x68);
    write_data(0x01);
    write_data(0xd5);
    write_data(0x01);
    write_data(0xd7);
    write_data(0x02);
    write_data(0x36);
    write_data(0x02);
    write_data(0xa6);
    write_data(0x02);
    write_data(0xee);
    write_data(0x03);
    write_data(0x48);
    write_data(0x03);
    write_data(0xa0);
    write_data(0x03);
    write_data(0xba);
    write_data(0x03);
    write_data(0xc5);
    write_data(0x03);
    write_data(0xd0);
    write_data(0x03);
    write_data(0xE0);
    write_data(0x03);
    write_data(0xea);
    write_data(0x03);
    write_data(0xFa);
    write_data(0x03);
    write_data(0xFF);

    write_cmd(0xD5);
    write_data(0x00);
    write_data(0x00);
    write_data(0x00);
    write_data(0x04);
    write_data(0x00);
    write_data(0x12);
    write_data(0x00);
    write_data(0x18);
    write_data(0x00);
    write_data(0x21);
    write_data(0x00);
    write_data(0x2a);
    write_data(0x00);
    write_data(0x35);
    write_data(0x00);
    write_data(0x47);
    write_data(0x00);
    write_data(0x56);
    write_data(0x00);
    write_data(0x90);
    write_data(0x00);
    write_data(0xe5);
    write_data(0x01);
    write_data(0x68);
    write_data(0x01);
    write_data(0xd5);
    write_data(0x01);
    write_data(0xd7);
    write_data(0x02);
    write_data(0x36);
    write_data(0x02);
    write_data(0xa6);
    write_data(0x02);
    write_data(0xee);
    write_data(0x03);
    write_data(0x48);
    write_data(0x03);
    write_data(0xa0);
    write_data(0x03);
    write_data(0xba);
    write_data(0x03);
    write_data(0xc5);
    write_data(0x03);
    write_data(0xd0);
    write_data(0x03);
    write_data(0xE0);
    write_data(0x03);
    write_data(0xea);
    write_data(0x03);
    write_data(0xFa);
    write_data(0x03);
    write_data(0xFF);

    write_cmd(0xD6);
    write_data(0x00);
    write_data(0x00);
    write_data(0x00);
    write_data(0x04);
    write_data(0x00);
    write_data(0x12);
    write_data(0x00);
    write_data(0x18);
    write_data(0x00);
    write_data(0x21);
    write_data(0x00);
    write_data(0x2a);
    write_data(0x00);
    write_data(0x35);
    write_data(0x00);
    write_data(0x47);
    write_data(0x00);
    write_data(0x56);
    write_data(0x00);
    write_data(0x90);
    write_data(0x00);
    write_data(0xe5);
    write_data(0x01);
    write_data(0x68);
    write_data(0x01);
    write_data(0xd5);
    write_data(0x01);
    write_data(0xd7);
    write_data(0x02);
    write_data(0x36);
    write_data(0x02);
    write_data(0xa6);
    write_data(0x02);
    write_data(0xee);
    write_data(0x03);
    write_data(0x48);
    write_data(0x03);
    write_data(0xa0);
    write_data(0x03);
    write_data(0xba);
    write_data(0x03);
    write_data(0xc5);
    write_data(0x03);
    write_data(0xd0);
    write_data(0x03);
    write_data(0xE0);
    write_data(0x03);
    write_data(0xea);
    write_data(0x03);
    write_data(0xFa);
    write_data(0x03);
    write_data(0xFF);

    write_cmd(0x3a);
    write_data(0x66);

    write_cmd(0x11); // Sleep Out
    ESP_LOGI(TAG, "--> Sleep Out sent. Waiting 200ms for charge pumps...");
    vTaskDelay(pdMS_TO_TICKS(200)); 
    write_cmd(0x29); // Display ON
    ESP_LOGI(TAG, "--> SPI Init Complete.");

    // --- STEP 4: DESTROY THE SPI BUS & CLAMP CS HIGH ---
    ESP_LOGI(TAG, "[4] Tearing down SPI Bus to free GPIOs...");
    spi_bus_remove_device(g_screen_spi);
    spi_bus_free(SPI2_HOST);
    
    // CRITICAL FIX: The moment SPI is freed, CS floats. If it floats low, the 
    // display reads the RGB lines as SPI commands and crashes. Clamping it HIGH!
    gpio_set_direction(LCD_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_SPI_CS, 1);
    
    gpio_reset_pin(LCD_SPI_CLK);
    gpio_reset_pin(LCD_SPI_MOSI);

    // --- STEP 5: START RGB DMA PUMP ---
    ESP_LOGI(TAG, "[5] Initializing ESP-LCD RGB Panel...");
    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.data_width = 16;
    panel_config.psram_trans_align = 64;
    panel_config.num_fbs = 1; 
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.disp_gpio_num = -1; 
    panel_config.pclk_gpio_num = LCD_PCLK;
    panel_config.vsync_gpio_num = LCD_VSYNC;
    panel_config.hsync_gpio_num = LCD_HSYNC;
    panel_config.de_gpio_num = LCD_DE;
    
    int data_pins[16] = {LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_D4, LCD_D5, LCD_D6, LCD_D7, 
                         LCD_D8, LCD_D9, LCD_D10, LCD_D11, LCD_D12, LCD_D13, LCD_D14, LCD_D15};
    for (int i = 0; i < 16; i++) {
        panel_config.data_gpio_nums[i] = data_pins[i];
    }
    
    panel_config.timings.pclk_hz = 10 * 1000 * 1000; // 10 MHz
    panel_config.timings.h_res = 480;
    panel_config.timings.v_res = 480;
    panel_config.timings.hsync_back_porch = 40;
    panel_config.timings.hsync_front_porch = 8;
    panel_config.timings.hsync_pulse_width = 10;
    panel_config.timings.vsync_back_porch = 40;
    panel_config.timings.vsync_front_porch = 8;
    panel_config.timings.vsync_pulse_width = 10;
    panel_config.timings.flags.hsync_idle_low = 1;
    panel_config.timings.flags.vsync_idle_low = 1;
    panel_config.timings.flags.pclk_active_neg = 0; 
    panel_config.flags.fb_in_psram = 1; 
    panel_config.bounce_buffer_size_px = 10 * 480; 

    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    vTaskDelay(pdMS_TO_TICKS(500));

    // --- STEP 6: DRAW SOLID GREEN ---
    ESP_LOGI(TAG, "[6] Drawing Solid Green...");
    uint16_t *color_map = (uint16_t *)heap_caps_malloc(480 * 480 * 2, MALLOC_CAP_SPIRAM);
    for(int i = 0; i < 480 * 480; i++) {
        color_map[i] = 0x07E0; // Standard RGB565 Green
    }
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 480, 480, color_map);

    // --- STEP 7: IGNITE BACKLIGHT ---
    ESP_LOGI(TAG, "[7] Igniting Backlight...");
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 1);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "SUCCESS! SCREEN SHOULD BE SOLID GREEN.");
    ESP_LOGI(TAG, "========================================");

    while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}