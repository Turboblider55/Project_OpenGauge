idf_component_register(SRCS "app_main.cpp" INCLUDE_DIRS "." REQUIRES og_protocol og_obd og_common disp_smooth disp_lcd
                       PRIV_REQUIRES esp_timer)
