#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"

// Include LovyanGFX natively
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

static const char *TAG = "LGFX_TEST";

// ========================================================
// 1. THE 9-BIT SPI WAKE-UP HACK (Direct from Arduino)
// ========================================================
spi_device_handle_t g_screen_spi;

static void write_cmd(uint16_t cmd) {
    spi_transaction_ext_t trans = {};
    trans.base.flags = SPI_TRANS_VARIABLE_CMD;
    trans.base.cmd = (cmd | 0x0000);
    trans.command_bits = 9;
    ESP_ERROR_CHECK(spi_device_transmit(g_screen_spi, (spi_transaction_t*)&trans));
}

static void write_data(uint16_t data) {
    spi_transaction_ext_t trans = {};
    trans.base.flags = SPI_TRANS_VARIABLE_CMD;
    trans.base.cmd = (data | 0x0100);
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

    // Hex Sequence
    write_cmd(0xF0); write_data(0x55); write_data(0xAA); write_data(0x52); write_data(0x08); write_data(0x00);
    write_cmd(0xF6); write_data(0x5A); write_data(0x87);
    write_cmd(0xC1); write_data(0x3F);
    write_cmd(0xC2); write_data(0x0E);
    write_cmd(0xC6); write_data(0xF8);
    write_cmd(0xC9); write_data(0x10);
    write_cmd(0xCD); write_data(0x25);
    write_cmd(0xF8); write_data(0x8A);
    write_cmd(0xAC); write_data(0x45);
    write_cmd(0xA0); write_data(0xDD);
    write_cmd(0xA7); write_data(0x47);
    write_cmd(0xFA); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04);
    write_cmd(0x86); write_data(0x99); write_data(0xa3); write_data(0xa3); write_data(0x51);
    write_cmd(0xA3); write_data(0xEE);
    write_cmd(0xFD); write_data(0x28); write_data(0x28); write_data(0x00);
    write_cmd(0x71); write_data(0x48);
    write_cmd(0x72); write_data(0x48);
    write_cmd(0x73); write_data(0x00); write_data(0x44);
    write_cmd(0x97); write_data(0xEE);
    write_cmd(0x83); write_data(0x93);
    write_cmd(0x9A); write_data(0x72);
    write_cmd(0x9B); write_data(0x5a);
    write_cmd(0x82); write_data(0x2c); write_data(0x2c);
    write_cmd(0xB1); write_data(0x10);

    write_cmd(0x6D); write_data(0x00); write_data(0x1F); write_data(0x19); write_data(0x1A); write_data(0x10); write_data(0x0e); write_data(0x0c); write_data(0x0a); write_data(0x02); write_data(0x07); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x1E); write_data(0x08); write_data(0x01); write_data(0x09); write_data(0x0b); write_data(0x0D); write_data(0x0F); write_data(0x1a); write_data(0x19); write_data(0x1f); write_data(0x00);
    write_cmd(0x64); write_data(0x38); write_data(0x05); write_data(0x01); write_data(0xdb); write_data(0x03); write_data(0x03); write_data(0x38); write_data(0x04); write_data(0x01); write_data(0xdc); write_data(0x03); write_data(0x03); write_data(0x7A); write_data(0x7A); write_data(0x7A); write_data(0x7A);
    write_cmd(0x65); write_data(0x38); write_data(0x03); write_data(0x01); write_data(0xdd); write_data(0x03); write_data(0x03); write_data(0x38); write_data(0x02); write_data(0x01); write_data(0xde); write_data(0x03); write_data(0x03); write_data(0x7A); write_data(0x7A); write_data(0x7A); write_data(0x7A);
    write_cmd(0x66); write_data(0x38); write_data(0x01); write_data(0x01); write_data(0xdf); write_data(0x03); write_data(0x03); write_data(0x38); write_data(0x00); write_data(0x01); write_data(0xe0); write_data(0x03); write_data(0x03); write_data(0x7A); write_data(0x7A); write_data(0x7A); write_data(0x7A);
    write_cmd(0x67); write_data(0x30); write_data(0x01); write_data(0x01); write_data(0xe1); write_data(0x03); write_data(0x03); write_data(0x30); write_data(0x02); write_data(0x01); write_data(0xe2); write_data(0x03); write_data(0x03); write_data(0x7A); write_data(0x7A); write_data(0x7A); write_data(0x7A);
    write_cmd(0x68); write_data(0x00); write_data(0x08); write_data(0x15); write_data(0x08); write_data(0x15); write_data(0x7A); write_data(0x7A); write_data(0x08); write_data(0x15); write_data(0x08); write_data(0x15); write_data(0x7A); write_data(0x7A);
    write_cmd(0x60); write_data(0x38); write_data(0x08); write_data(0x7A); write_data(0x7A); write_data(0x38); write_data(0x09); write_data(0x7A); write_data(0x7A);
    write_cmd(0x63); write_data(0x31); write_data(0xe4); write_data(0x7A); write_data(0x7A); write_data(0x31); write_data(0xe5); write_data(0x7A); write_data(0x7A);
    write_cmd(0x69); write_data(0x04); write_data(0x22); write_data(0x14); write_data(0x22); write_data(0x14); write_data(0x22); write_data(0x08);
    write_cmd(0x6B); write_data(0x07);
    write_cmd(0x7A); write_data(0x08); write_data(0x13);
    write_cmd(0x7B); write_data(0x08); write_data(0x13);

    write_cmd(0xD1); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD2); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD3); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD4); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD5); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);
    write_cmd(0xD6); write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x04); write_data(0x00); write_data(0x12); write_data(0x00); write_data(0x18); write_data(0x00); write_data(0x21); write_data(0x00); write_data(0x2a); write_data(0x00); write_data(0x35); write_data(0x00); write_data(0x47); write_data(0x00); write_data(0x56); write_data(0x00); write_data(0x90); write_data(0x00); write_data(0xe5); write_data(0x01); write_data(0x68); write_data(0x01); write_data(0xd5); write_data(0x01); write_data(0xd7); write_data(0x02); write_data(0x36); write_data(0x02); write_data(0xa6); write_data(0x02); write_data(0xee); write_data(0x03); write_data(0x48); write_data(0x03); write_data(0xa0); write_data(0x03); write_data(0xba); write_data(0x03); write_data(0xc5); write_data(0x03); write_data(0xd0); write_data(0x03); write_data(0xE0); write_data(0x03); write_data(0xea); write_data(0x03); write_data(0xFa); write_data(0x03); write_data(0xFF);

    write_cmd(0x3A); write_data(0x66);

    write_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    write_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    spi_bus_remove_device(g_screen_spi);
    spi_bus_free(SPI2_HOST);
}

// ========================================================
// 2. LOVYANGFX ARCHITECTURE OVERRIDE
// ========================================================
class Panel_BC02_Spec : public lgfx::Panel_RGB {
public:
    // This override forces the SPI wake-up sequence to happen EXACTLY 
    // synchronously with the LovyanGFX RGB PCLK initialization.
    bool init(bool use_reset) override {
        ESP_LOGI(TAG, "--> Panel_BC02_Spec Override: Disabling JTAG Trap...");
        gpio_reset_pin((gpio_num_t)39);
        gpio_reset_pin((gpio_num_t)40);
        gpio_reset_pin((gpio_num_t)41);
        gpio_reset_pin((gpio_num_t)42);

        ESP_LOGI(TAG, "--> Panel_BC02_Spec Override: Asserting Display Enable...");
        gpio_set_direction((gpio_num_t)41, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)41, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        ESP_LOGI(TAG, "--> Panel_BC02_Spec Override: Injecting SPI Hack...");
        panelLan_rgb_spi_init();

        ESP_LOGI(TAG, "--> Panel_BC02_Spec Override: Instantly starting RGB DMA...");
        if (!lgfx::Panel_RGB::init(false)) {
            ESP_LOGE(TAG, "FATAL: LovyanGFX RGB Init FAILED! (PSRAM Issue?)");
            return false;
        }
        return true;
    }
};

class LGFX : public lgfx::LGFX_Device {
    Panel_BC02_Spec _panel_instance; // Custom Override Class!
    lgfx::Bus_RGB   _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX(void) {
        {
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
        }
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width  = 480;
            cfg.panel_width   = 480;
            cfg.memory_height = 480;
            cfg.panel_height  = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 5;
            cfg.invert = false;
            cfg.freq   = 500;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        setPanel(&_panel_instance);
    }
};

LGFX tft; // MUST Remain Global!

// ========================================================
// 3. MAIN ENTRY POINT
// ========================================================
extern "C" void app_main() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "STARTING LOVYANGFX OVERRIDE TEST");
    ESP_LOGI(TAG, "========================================");

    // Failsafe: Check PSRAM Health
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 1000000) {
        ESP_LOGE(TAG, "FATAL ERROR: PSRAM is not initialized properly!");
        return;
    } else {
        ESP_LOGI(TAG, "PSRAM Health OK. Free size: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    ESP_LOGI(TAG, "Initializing LovyanGFX...");
    tft.init();
    tft.setBrightness(200);

    ESP_LOGI(TAG, "Drawing to Screen...");
    tft.fillScreen(TFT_BLUE);
    
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setTextSize(3.0);
    tft.drawString("Hello ESP-IDF!", 240, 240);

    ESP_LOGI(TAG, "SUCCESS! CHECK THE SCREEN.");
    
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}