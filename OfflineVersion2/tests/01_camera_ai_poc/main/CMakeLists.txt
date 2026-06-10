idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp32-camera esp-dl driver esp_timer esp_partition mbedtls)