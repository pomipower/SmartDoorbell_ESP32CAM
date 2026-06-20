idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_event esp_netif esp_websocket_client esp32-camera driver nvs_flash mbedtls)