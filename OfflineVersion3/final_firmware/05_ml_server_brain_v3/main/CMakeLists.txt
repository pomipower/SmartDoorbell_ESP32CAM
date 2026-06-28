idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server nvs_flash spiffs esp_wifi esp-dl esp32-camera esp_timer LovyanGFX lvgl)

# Suppress the strict ESP-IDF warnings triggered by the esp-dl library
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=format" "-Wno-error=misleading-indentation")