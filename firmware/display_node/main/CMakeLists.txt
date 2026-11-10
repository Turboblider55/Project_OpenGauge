idf_component_register(SRCS "app_main.cpp" "color_test.cpp" "lvgl_setup.cpp" "lvgl_test.cpp" "espnow_rx_test.cpp"
                            "dash_app.cpp" "touch_test.cpp" "battery_adc.cpp" "wake_test.cpp"
                            "fonts/og_font_montserrat_20.c" "fonts/og_font_montserrat_28.c"
                            "logo/og_logos.cpp" "logo/og_logo_opel.c" "logo/og_logo_volkswagen.c" "logo/og_logo_skoda.c"
                            "logo/og_logo_suzuki.c" "logo/og_logo_toyota.c" "logo/og_logo_peugeot.c" "logo/og_logo_renault.c"
                            "logo/og_logo_bmw.c" "settings_store.cpp" "settings_view.cpp" "og_prof.cpp" "lv_mem_psram.c"
                       INCLUDE_DIRS "." "fonts" "logo"
                       REQUIRES og_protocol og_obd og_common og_espnow og_led og_ble disp_smooth disp_lcd disp_model esp_adc
                       PRIV_REQUIRES nvs_flash esp_timer esp_hw_support esp_driver_gpio)

# Az LVGL az egyedi memóriakezelő függvényeit (lv_mem_psram.c) hivatkozza, de a main komponens a linkelési sorrendben előbb áll:
# a -u kikényszeríti, hogy az objektum bekerüljön.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")
