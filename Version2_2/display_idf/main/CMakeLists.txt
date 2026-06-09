idf_component_register(SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi esp_event esp_netif nvs_flash
        espressif__esp_websocket_client
        espressif__esp_jpeg
        json
        lvgl__lvgl
        esp_lcd driver
        espressif__esp_lcd_gc9503
        espressif__esp_lcd_panel_io_additions
        esp_timer esp_system)