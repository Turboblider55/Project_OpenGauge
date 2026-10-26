idf_component_register(SRCS "app_main.cpp" "color_test.cpp" "lvgl_setup.cpp" "lvgl_test.cpp" "espnow_rx_test.cpp"
                            "dash_app.cpp" "touch_test.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES og_protocol og_obd og_common og_espnow disp_smooth disp_lcd disp_model
                       PRIV_REQUIRES esp_timer)
