idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_websocket_client json esp_wifi nvs_flash esp_event esp_netif esp_http_server)