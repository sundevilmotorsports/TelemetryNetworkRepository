idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES esp_wifi esp_driver_gpio esp_driver_ledc nvs_flash esp_timer
                    INCLUDE_DIRS ".")
