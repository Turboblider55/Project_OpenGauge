idf_component_register(SRCS "app_main.cpp" "color_test.cpp" "lvgl_setup.cpp" "lvgl_test.cpp" "espnow_rx_test.cpp"
                            "dash_app.cpp" "touch_test.cpp" "battery_adc.cpp" "wake_test.cpp"
                            "fonts/og_font_montserrat_20.c" "fonts/og_font_montserrat_28.c"
                            "logo/og_logo.c"
                       INCLUDE_DIRS "." "fonts" "logo"
                       REQUIRES og_protocol og_obd og_common og_espnow og_led og_ble disp_smooth disp_lcd disp_model esp_adc
                       PRIV_REQUIRES esp_timer esp_hw_support esp_driver_gpio)
