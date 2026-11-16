idf_component_register(SRCS "src/status_led.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES og_common
                       PRIV_REQUIRES esp_driver_ledc esp_timer esp_system)
