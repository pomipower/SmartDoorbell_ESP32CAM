// ==============================================================================
//  SMART DOORBELL — INDOOR DISPLAY
//  Board  : PANLEE BC02 (ZX3D95CE01S-TR-V12)
//  Module : WT32-S3-WROVER-N16R8  (16 MB Flash, 8 MB OPI PSRAM)
//  IDF    : v5.2.2   |   LVGL : v8.3.x
//  Goal   : Receive JPEG stream from FastAPI WebSocket, decode, display.
// ==============================================================================
//
//  Task layout
//  ───────────
//  Core 0 │ lvgl_task        (prio 5)  — calls lv_timer_handler every 5 ms
//  Core 1 │ jpeg_decode_task (prio 4)  — wakes on xTaskNotifyGive, decodes JPEG
//  Core 1 │ app_main         (prio 1)  — init only, then idles
//  WS     │ esp_websocket_client spawns its own task (prio 6)
//           WS event handler reassembles JPEG fragments, notifies decode task.
//
//  Memory layout
//  ─────────────
//  PSRAM  │ ws_rx_buf[2]    2 × 64 KB  — ping-pong WebSocket receive slots
//         │ dec_buf[2]      2 × 150 KB — ping-pong decoded RGB565 frames
//  iSRAM  │ lvgl_draw_mem   480×20×2 B — LVGL partial render buffer
//
//  Colour math (why swap_color_bytes = 1)
//  ──────────────────────────────────────
//  esp_jpeg_decode native RGB565 = big-endian: byte[0]=RRRRRGGG, byte[1]=GGGBBBBB
//  lv_color_t on little-endian ESP32-S3 without LV_COLOR_16_SWAP stores
//  pure-red as byte[0]=0x00, byte[1]=0xF8 → needs little-endian RGB565.
//  swap_color_bytes=1 converts the JPEG output to little-endian → correct.
// ==============================================================================

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "esp_websocket_client.h"
#include "jpeg_decoder.h"

#include "lvgl.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_gc9503.h"
#include "esp_timer.h"
#include "driver/gpio.h"

// ─── Tag for all log lines ────────────────────────────────────────────────────
static const char *TAG = "DOORBELL";

// ─── User config — edit these ────────────────────────────────────────────────
#define WIFI_SSID   "Galaxy S24"
#define WIFI_PASS   "qwne3344"
#define WS_URI      "ws://10.151.110.250:5050/ws/frontend"

// ─── Display pins — PANLEE BC02 datasheet Tab.3, do NOT change ───────────────
//     3-wire SPI is used only during GC9503V register init.
//     PIN_SPI_SCL and PIN_SPI_SDA are shared with RGB_D0 / RGB_D1.
//     The gc9503 driver releases those pins for RGB after init.
#define PIN_BL          5       // Backlight, active-high

#define PIN_SPI_CS      38      // GC9503V SPI chip-select
#define PIN_SPI_SCL     45      // GC9503V SPI clock  (= RGB D0)
#define PIN_SPI_SDA     48      // GC9503V SPI MOSI   (= RGB D1)

#define PIN_PCLK        39
#define PIN_DE          40
#define PIN_VSYNC       41
#define PIN_HSYNC       42

#define PIN_D0          45
#define PIN_D1          48
#define PIN_D2          47
#define PIN_D3           0
#define PIN_D4          21
#define PIN_D5          14
#define PIN_D6          13
#define PIN_D7          12
#define PIN_D8          11
#define PIN_D9          16
#define PIN_D10         17
#define PIN_D11         18
#define PIN_D12          8
#define PIN_D13          3
#define PIN_D14         46
#define PIN_D15         10

// ─── Display geometry ─────────────────────────────────────────────────────────
#define DISP_W          480
#define DISP_H          480
#define PCLK_HZ         (16 * 1000 * 1000)

// Rows in the LVGL partial-render buffer (lives in internal SRAM).
// 20 rows × 480 × 2 bytes = 19 200 B.  Increase to 40 if rendering is slow.
#define LVGL_BUF_ROWS   20

// ─── Server stream config ─────────────────────────────────────────────────────
// Server encodes at 320×240 QVGA (see video_processing_loop in server script).
#define FRAME_W         320
#define FRAME_H         240

// Where to place the 320×240 frame on the 480×480 screen (centred).
#define FRAME_X         ((DISP_W - FRAME_W) / 2)   // 80 px
#define FRAME_Y         ((DISP_H - FRAME_H) / 2)   // 120 px

// Each WS receive slot: 64 KB is well above a typical QVGA JPEG (~10–25 KB).
#define JPEG_BUF_SZ     (64 * 1024)

// ─── LVGL mutex ───────────────────────────────────────────────────────────────
// Every lv_* call must be bracketed by Take / Give of this mutex.
static SemaphoreHandle_t s_lvgl_mux = NULL;

// ─── Ping-pong WebSocket receive buffers (PSRAM) ─────────────────────────────
// s_ws_wr  : WS event handler writes into this slot.
// s_ws_rd  : decode task reads from this slot (-1 = nothing ready).
static uint8_t  *s_ws_buf[2]  = {NULL, NULL};
static uint32_t  s_ws_len[2]  = {0, 0};
static volatile uint8_t s_ws_wr   = 0;
static volatile int8_t  s_ws_rd   = -1;
static volatile bool    s_in_frm  = false;   // mid-reassembly flag

// ─── Ping-pong decoded-frame buffers (PSRAM) ─────────────────────────────────
// s_disp_slot : LVGL is reading from this slot (do NOT write).
// s_dec_slot  : decode task writes into this slot (never touched by LVGL).
static uint8_t      *s_dec_buf[2]   = {NULL, NULL};
static lv_img_dsc_t  s_dec_dsc[2];
static volatile uint8_t s_disp_slot = 0;
static volatile uint8_t s_dec_slot  = 1;

// ─── Handles ──────────────────────────────────────────────────────────────────
static TaskHandle_t               s_dec_task = NULL;
static esp_websocket_client_handle_t s_ws    = NULL;
static volatile bool              s_wifi_up  = false;

// ─── LVGL widgets ─────────────────────────────────────────────────────────────
static lv_obj_t *s_lbl  = NULL;   // status label (visible during boot / error)
static lv_obj_t *s_img  = NULL;   // video image  (visible while streaming)


// ==============================================================================
//  LCD / LVGL helpers
// ==============================================================================

// flush_cb — called by LVGL when a region is ready to paint.
// Copies the partial LVGL render buffer to the RGB panel framebuffer (PSRAM).
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *buf)
{
    // Log the first 3 calls so we can confirm LVGL is actually rendering.
    // If these lines never appear, lv_timer_handler is not triggering renders.
    static int call_n = 0;
    if (call_n < 3) {
        ESP_LOGI(TAG, "flush_cb #%d  area=(%d,%d)-(%d,%d)",
                 ++call_n, area->x1, area->y1, area->x2, area->y2);
    }

    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_err_t err = esp_lcd_panel_draw_bitmap(
        panel,
        area->x1, area->y1,
        area->x2 + 1, area->y2 + 1,
        buf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap failed: %s", esp_err_to_name(err));
    }
    lv_disp_flush_ready(drv);
}

// lv_tick_cb — driven by a 2 ms ESP timer, feeds the LVGL clock.
// Safe to call from a timer ISR context (it's just an increment).
static void lv_tick_cb(void *arg)
{
    lv_tick_inc(2);
}

// lvgl_task — sole owner of lv_timer_handler; runs on Core 0.
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started (core %d)", xPortGetCoreID());
    while (1) {
        // Try to take mutex; if another task holds it briefly, we skip a cycle.
        if (xSemaphoreTake(s_lvgl_mux, pdMS_TO_TICKS(20)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(s_lvgl_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// set_status — update the status label from any task, thread-safe.
// Also ensures the label is visible and the video widget is hidden.
static void set_status(const char *msg)
{
    ESP_LOGI(TAG, ">> %s", msg);
    if (xSemaphoreTake(s_lvgl_mux, pdMS_TO_TICKS(1000)) == pdTRUE) {
        lv_label_set_text(s_lbl, msg);
        lv_obj_clear_flag(s_lbl, LV_OBJ_FLAG_HIDDEN);
        if (s_img) lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        xSemaphoreGive(s_lvgl_mux);
    } else {
        ESP_LOGW(TAG, "set_status: mutex timeout");
    }
}


// ==============================================================================
//  JPEG decode task (Core 1)
// ==============================================================================

static void jpeg_decode_task(void *arg)
{
    ESP_LOGI(TAG, "Decode task started (core %d)", xPortGetCoreID());

    while (1) {
        // Block until the WS handler signals a complete JPEG frame.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (s_ws_rd < 0) continue;

        uint8_t rx  = (uint8_t)s_ws_rd;   // source WS slot
        uint8_t dst = s_dec_slot;          // destination decoded slot

        // ── Decode JPEG → RGB565 ──────────────────────────────────────────────
        // No mutex needed here: we write dec_buf[dst] while LVGL reads
        // dec_buf[s_disp_slot].  The ping-pong guarantee: dst ≠ s_disp_slot.
        esp_jpeg_image_cfg_t cfg = {
            .indata       = s_ws_buf[rx],
            .indata_size  = s_ws_len[rx],
            .outbuf       = s_dec_buf[dst],
            .outbuf_size  = FRAME_W * FRAME_H * 2,
            .out_format   = JPEG_IMAGE_FORMAT_RGB565,
            .out_scale    = JPEG_IMAGE_SCALE_0,
            .flags        = { .swap_color_bytes = 1 },  // match LVGL byte order
        };
        esp_jpeg_image_output_t out = {0};
        esp_err_t err = esp_jpeg_decode(&cfg, &out);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "JPEG decode error: %s (rx_len=%lu)",
                     esp_err_to_name(err), (unsigned long)s_ws_len[rx]);
            continue;
        }

        // Sanity-check output dimensions (server should always send 320×240).
        if (out.width != FRAME_W || out.height != FRAME_H) {
            ESP_LOGW(TAG, "Unexpected frame size %dx%d (expected %dx%d)",
                     out.width, out.height, FRAME_W, FRAME_H);
            // Still display it — dimensions in the dsc are already fixed.
        }

        // ── Hand decoded frame to LVGL under mutex ────────────────────────────
        if (xSemaphoreTake(s_lvgl_mux, pdMS_TO_TICKS(100)) == pdTRUE) {

            // First frame ever: switch from status label to image widget.
            if (s_img && lv_obj_has_flag(s_img, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(s_lbl, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
                ESP_LOGI(TAG, "First frame displayed — stream is live");
            }

            // Swap slots: dst becomes the new display slot, the old display
            // slot becomes the next decode target.
            s_disp_slot = dst;
            s_dec_slot  = dst ^ 1;

            // Alternating pointer (&s_dec_dsc[0] ↔ &s_dec_dsc[1]) forces
            // LVGL to treat each frame as a new source and re-read pixels.
            lv_img_set_src(s_img, &s_dec_dsc[s_disp_slot]);

            xSemaphoreGive(s_lvgl_mux);
        } else {
            ESP_LOGW(TAG, "Decode task: LVGL mutex timeout, frame dropped");
        }
    }
}


// ==============================================================================
//  WebSocket event handler (runs inside the WS client task)
// ==============================================================================

static void ws_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {

    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected to %s", WS_URI);
        set_status("Connected — waiting for stream...");
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected (will auto-reconnect)");
        s_in_frm = false;
        set_status("Server disconnected — reconnecting...");
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error (transport/TLS)");
        s_in_frm = false;
        break;

    case WEBSOCKET_EVENT_DATA: {
        // ── Ignore empty events ──────────────────────────────────────────────
        if (d->data_len == 0 || d->data_ptr == NULL) break;

        // ── Ignore text frames (JSON state from the server) ──────────────────
        if (d->op_code == 0x01) break;

        // ── Ignore control frames (ping=0x09, pong=0x0A, close=0x08) ─────────
        if (d->op_code == 0x08 || d->op_code == 0x09 || d->op_code == 0x0A) break;

        // op_code 0x02 = binary frame start.  op_code 0x00 = continuation.
        // We use payload_offset == 0 to detect the real start of each frame
        // because the client library may report op_code=0x02 for every chunk
        // depending on version.

        if (d->payload_offset == 0) {
            // Beginning of a new binary frame — reset the write slot.
            s_ws_len[s_ws_wr] = 0;
            s_in_frm = true;
        }

        if (!s_in_frm) break;   // stray continuation with no matching start

        // ── Append chunk to the active receive slot ───────────────────────────
        uint32_t tail = s_ws_len[s_ws_wr];
        if (tail + (uint32_t)d->data_len > JPEG_BUF_SZ) {
            ESP_LOGW(TAG, "RX buffer overflow (%lu+%d > %d) — frame dropped",
                     (unsigned long)tail, d->data_len, JPEG_BUF_SZ);
            s_in_frm = false;
            s_ws_len[s_ws_wr] = 0;
            break;
        }
        memcpy(s_ws_buf[s_ws_wr] + tail, d->data_ptr, d->data_len);
        s_ws_len[s_ws_wr] = tail + (uint32_t)d->data_len;

        // ── Check for end of frame ────────────────────────────────────────────
        bool is_last = (d->payload_len > 0) &&
                       ((d->payload_offset + d->data_len) >= (uint32_t)d->payload_len);
        if (is_last) {
            s_in_frm = false;
            s_ws_rd  = (int8_t)s_ws_wr;    // hand this slot to the decoder
            s_ws_wr ^= 1;                   // swap to the other write slot
            s_ws_len[s_ws_wr] = 0;         // clear the incoming slot
            if (s_dec_task) xTaskNotifyGive(s_dec_task);
        }
        break;
    }

    default:
        break;
    }
}


// ==============================================================================
//  Wi-Fi event handler
// ==============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *event_data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started — connecting...");
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev =
            (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d) — retrying", ev->reason);
        s_wifi_up = false;
        esp_wifi_connect();

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_wifi_up = true;
    }
}


// ==============================================================================
//  LCD panel init  (GC9503V via 3-wire SPI + RGB parallel)
// ==============================================================================

static esp_lcd_panel_handle_t lcd_init(void)
{
    // ── Backlight ON ──────────────────────────────────────────────────────────
    gpio_reset_pin(PIN_BL);
    gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BL, 1);
    ESP_LOGI(TAG, "Backlight ON (GPIO %d)", PIN_BL);

    // ── 3-wire SPI IO handle — used ONLY for GC9503V register writes ──────────
    // After esp_lcd_panel_init() the SPI pins are released;
    // GPIO 45 and 48 revert to RGB_D0 / RGB_D1 automatically.
    spi_line_config_t spi_line = {
        .cs_io_type  = IO_TYPE_GPIO,  .cs_gpio_num  = PIN_SPI_CS,
        .scl_io_type = IO_TYPE_GPIO,  .scl_gpio_num = PIN_SPI_SCL,
        .sda_io_type = IO_TYPE_GPIO,  .sda_gpio_num = PIN_SPI_SDA,
        .io_expander = NULL,
    };
    esp_lcd_panel_io_3wire_spi_config_t io_cfg =
        GC9503_PANEL_IO_3WIRE_SPI_CONFIG(spi_line);
    esp_lcd_panel_io_handle_t io = NULL;

    esp_err_t err = esp_lcd_new_panel_io_3wire_spi(&io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "3-wire SPI IO create failed: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }
    ESP_LOGI(TAG, "3-wire SPI IO created (CS=%d SCL=%d SDA=%d)",
             PIN_SPI_CS, PIN_SPI_SCL, PIN_SPI_SDA);

    // ── RGB parallel panel config ─────────────────────────────────────────────
    // Framebuffer lives in PSRAM (fb_in_psram=true).
    // bounce_buffer_size_px allocates 2×N×2 bytes of internal SRAM so the
    // LCD DMA does not read PSRAM directly (required on ESP32-S3).
    esp_lcd_rgb_panel_config_t rgb_cfg = {
        .data_width            = 16,
        .bounce_buffer_size_px = DISP_W * 10,   // 2 × 9600 B internal SRAM
        .clk_src               = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num         = GPIO_NUM_NC,   // no separate DISP_EN pin
        .pclk_gpio_num         = PIN_PCLK,
        .vsync_gpio_num        = PIN_VSYNC,
        .hsync_gpio_num        = PIN_HSYNC,
        .de_gpio_num           = PIN_DE,
        .data_gpio_nums        = {
            PIN_D0,  PIN_D1,  PIN_D2,  PIN_D3,
            PIN_D4,  PIN_D5,  PIN_D6,  PIN_D7,
            PIN_D8,  PIN_D9,  PIN_D10, PIN_D11,
            PIN_D12, PIN_D13, PIN_D14, PIN_D15,
        },
        .timings = {
            // GC9503V 480×480 — values from Espressif's own GC9503 reference
            // design (esp-bsp / ESP32-S3-LCD-EV-Board).  HBP=40 / VBP=8 are
            // the critical ones; smaller values caused sync failure.
            .pclk_hz           = PCLK_HZ,
            .h_res             = DISP_W,
            .v_res             = DISP_H,
            .hsync_pulse_width = 4,
            .hsync_back_porch  = 40,
            .hsync_front_porch = 20,
            .vsync_pulse_width = 4,
            .vsync_back_porch  = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = true,
        },
        .flags.fb_in_psram = true,
    };

    esp_lcd_panel_handle_t panel = NULL;

    err = esp_lcd_new_panel_gc9503(io, &rgb_cfg, &panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GC9503 panel create failed: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }

    err = esp_lcd_panel_reset(panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }

    err = esp_lcd_panel_init(panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }

    // NOTE: esp_lcd_panel_disp_on_off is NOT implemented by the gc9503 driver
    // (returns ESP_ERR_NOT_SUPPORTED). DISPON (0x29) is already issued inside
    // esp_lcd_panel_init() as part of the built-in GC9503V init sequence.
    // Do NOT call esp_lcd_panel_disp_on_off here.

    ESP_LOGI(TAG, "GC9503V panel ready: %dx%d @ %d MHz PCLK",
             DISP_W, DISP_H, (int)(PCLK_HZ / 1000000));

    // ── DIAGNOSTIC: write solid green directly — completely bypasses LVGL ─────
    // GREEN on screen  →  LCD path (GC9503V + RGB DMA) works; LVGL is at fault.
    // BLACK screen     →  Panel init or timing is still wrong; fix that first.
    // 0x07E0 = RGB565 pure green (R=0, G=63, B=0), correct for little-endian.
    {
        static uint16_t test_line[DISP_W];
        for (int i = 0; i < DISP_W; i++) test_line[i] = 0x07E0;
        for (int y = 0; y < DISP_H; y++) {
            esp_lcd_panel_draw_bitmap(panel, 0, y, DISP_W, y + 1, test_line);
        }
        ESP_LOGI(TAG, "DIAG: green fill sent — screen should be solid green for 2 s");
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "DIAG: 2 s elapsed — LVGL will now overwrite with its UI");
    }

    return panel;
}


// ==============================================================================
//  LVGL init — display driver, tick timer, UI widgets
// ==============================================================================

static void lvgl_init(esp_lcd_panel_handle_t panel)
{
    lv_init();

    // ── Draw buffer in internal SRAM — LVGL renders partial strips here ───────
    // Static so it persists for the life of the program.
    static lv_color_t s_draw_mem[DISP_W * LVGL_BUF_ROWS];
    static lv_disp_draw_buf_t s_draw_buf;
    lv_disp_draw_buf_init(&s_draw_buf, s_draw_mem, NULL, DISP_W * LVGL_BUF_ROWS);
    ESP_LOGI(TAG, "LVGL draw buf: %u B in internal SRAM",
             (unsigned)sizeof(s_draw_mem));

    // ── Register display driver ───────────────────────────────────────────────
    static lv_disp_drv_t s_drv;
    lv_disp_drv_init(&s_drv);
    s_drv.hor_res   = DISP_W;
    s_drv.ver_res   = DISP_H;
    s_drv.flush_cb  = flush_cb;
    s_drv.draw_buf  = &s_draw_buf;
    s_drv.user_data = panel;
    lv_disp_t *disp = lv_disp_drv_register(&s_drv);
    if (!disp) {
        ESP_LOGE(TAG, "LVGL driver register failed — aborting");
        esp_restart();
    }
    ESP_LOGI(TAG, "LVGL display driver registered (%dx%d)", DISP_W, DISP_H);

    // ── LVGL tick via ESP timer (2 ms) ────────────────────────────────────────
    const esp_timer_create_args_t tick_args = {
        .callback = lv_tick_cb,
        .name     = "lv_tick",
    };
    esp_timer_handle_t tick_tmr;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_tmr));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_tmr, 2000)); // 2000 µs
    ESP_LOGI(TAG, "LVGL tick timer started (2 ms)");

    // ── Screen: solid black ───────────────────────────────────────────────────
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // ── Status label — white text, centred, wraps to two lines ───────────────
    s_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl, lv_color_white(), LV_PART_MAIN);
    lv_label_set_long_mode(s_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl, DISP_W - 20);
    lv_label_set_text(s_lbl, "Initializing...");
    lv_obj_align(s_lbl, LV_ALIGN_CENTER, 0, 0);

    // ── Video image widget — hidden until the first frame arrives ─────────────
    s_img = lv_img_create(scr);
    lv_obj_align(s_img, LV_ALIGN_CENTER, 0, 0);    // centred → black borders
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);

    // ── Pre-fill ping-pong lv_img_dsc_t descriptors ───────────────────────────
    // dec_buf[i] is already allocated before lvgl_init is called.
    // Alternating which descriptor pointer we pass to lv_img_set_src() each
    // frame forces LVGL to see a new source and re-read pixel data — this is
    // the key trick that makes video work without stale caches.
    for (int i = 0; i < 2; i++) {
        s_dec_dsc[i].header.always_zero = 0;
        s_dec_dsc[i].header.cf          = LV_IMG_CF_TRUE_COLOR;
        s_dec_dsc[i].header.w           = FRAME_W;
        s_dec_dsc[i].header.h           = FRAME_H;
        s_dec_dsc[i].data_size          = FRAME_W * FRAME_H * 2;
        s_dec_dsc[i].data               = (const uint8_t *)s_dec_buf[i];
    }

    ESP_LOGI(TAG, "LVGL UI ready — showing status label");
}


// ==============================================================================
//  app_main
// ==============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  Smart Doorbell Indoor Display");
    ESP_LOGI(TAG, "  Board : PANLEE BC02 / WT32-S3-WROVER-N16R8");
    ESP_LOGI(TAG, "  Free heap at boot: %lu B", esp_get_free_heap_size());
    ESP_LOGI(TAG, "============================================");

    // ── 1. Allocate PSRAM buffers FIRST (before any init) ────────────────────
    // Two WS receive slots + two decoded-frame slots.
    s_ws_buf[0]  = heap_caps_malloc(JPEG_BUF_SZ,          MALLOC_CAP_SPIRAM);
    s_ws_buf[1]  = heap_caps_malloc(JPEG_BUF_SZ,          MALLOC_CAP_SPIRAM);
    s_dec_buf[0] = heap_caps_malloc(FRAME_W * FRAME_H * 2, MALLOC_CAP_SPIRAM);
    s_dec_buf[1] = heap_caps_malloc(FRAME_W * FRAME_H * 2, MALLOC_CAP_SPIRAM);

    if (!s_ws_buf[0] || !s_ws_buf[1] || !s_dec_buf[0] || !s_dec_buf[1]) {
        // This is fatal — nothing will work without these buffers.
        ESP_LOGE(TAG, "PSRAM alloc FAILED.  Free PSRAM: %zu B",
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        ESP_LOGE(TAG, "Check CONFIG_SPIRAM=y, CONFIG_SPIRAM_MODE_OCT=y in sdkconfig.");
        return; // Halt; no recovery possible.
    }
    ESP_LOGI(TAG, "PSRAM alloc OK. Free PSRAM: %zu B  (4 bufs: 2×%d + 2×%d)",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             JPEG_BUF_SZ, FRAME_W * FRAME_H * 2);

    // ── 2. LVGL mutex ─────────────────────────────────────────────────────────
    s_lvgl_mux = xSemaphoreCreateMutex();
    if (!s_lvgl_mux) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex — aborting");
        return;
    }

    // ── 3. LCD panel init ─────────────────────────────────────────────────────
    // ESP_ERROR_CHECK inside lcd_init() will reboot on any failure.
    esp_lcd_panel_handle_t panel = lcd_init();

    // ── 4. LVGL init (needs dec_buf allocated and panel ready) ────────────────
    lvgl_init(panel);

    // ── 5. Start LVGL task BEFORE Wi-Fi so "Initializing…" renders now ────────
    BaseType_t xret;
    xret = xTaskCreatePinnedToCore(
        lvgl_task, "lvgl", 8192, NULL, 5, NULL, 0);
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        esp_restart();
    }

    // Small delay so LVGL renders one full frame before we proceed.
    vTaskDelay(pdMS_TO_TICKS(50));

    // ── 6. JPEG decode task ───────────────────────────────────────────────────
    xret = xTaskCreatePinnedToCore(
        jpeg_decode_task, "jpeg_dec", 16384, NULL, 4, &s_dec_task, 1);
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create decode task");
        esp_restart();
    }

    // ── 7. NVS flash ──────────────────────────────────────────────────────────
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition dirty — erasing and reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // ── 8. Wi-Fi ──────────────────────────────────────────────────────────────
    set_status("Connecting to Wi-Fi...");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t sta = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    // Connection attempt is triggered by WIFI_EVENT_STA_START in handler.

    // Wait up to 30 s for an IP address.
    for (int i = 0; i < 300 && !s_wifi_up; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!s_wifi_up) {
        ESP_LOGE(TAG, "Wi-Fi failed after 30 s.  SSID: %s", WIFI_SSID);
        set_status("Wi-Fi failed.\nCheck SSID / password.");
        // Keep running so LVGL can display the error.
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // ── 9. WebSocket client ───────────────────────────────────────────────────
    set_status("Connecting to server...");

    esp_websocket_client_config_t ws_cfg = {
        .uri         = WS_URI,
        .buffer_size  = 8192,   // TCP receive buffer per read; frames are
                                // reassembled across multiple DATA events.
        .task_stack   = 8192,
        .task_prio    = 6,      // above decode (4) and lvgl (5) so data arrives
                                // promptly without starving the display.
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms   = 10000,
    };
    s_ws = esp_websocket_client_init(&ws_cfg);
    if (!s_ws) {
        ESP_LOGE(TAG, "WebSocket client init returned NULL");
        set_status("WebSocket init failed.");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_ERROR_CHECK(esp_websocket_register_events(
        s_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL));
    ESP_ERROR_CHECK(esp_websocket_client_start(s_ws));
    ESP_LOGI(TAG, "WebSocket client started → %s", WS_URI);

    // ── 10. Main-task idle loop ───────────────────────────────────────────────
    // Print a heartbeat every 10 s; everything else is driven by tasks/events.
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG,
                 "[heartbeat] heap_free=%lu  psram_free=%zu  wifi=%s  ws=%s",
                 esp_get_free_heap_size(),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 s_wifi_up ? "UP" : "DOWN",
                 (s_ws && esp_websocket_client_is_connected(s_ws)) ? "CONNECTED" : "DISCONNECTED");
    }
}