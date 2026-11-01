idf_component_register(SRCS "app_main.cpp" "color_test.cpp" "lvgl_setup.cpp" "lvgl_test.cpp" "espnow_rx_test.cpp"
                            "dash_app.cpp" "touch_test.cpp" "battery_adc.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES og_protocol og_obd og_common og_espnow og_led og_ble disp_smooth disp_lcd disp_model esp_adc
                       PRIV_REQUIRES esp_timer)
