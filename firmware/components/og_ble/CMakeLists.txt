idf_component_register(SRCS "src/ble_radio.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES bt nvs_flash esp_timer)
