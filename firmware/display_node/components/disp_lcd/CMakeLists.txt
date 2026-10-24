idf_component_register(SRCS "src/lcd_panel.cpp" "src/st77916_waveshare_1_85.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd
                       PRIV_REQUIRES esp_driver_spi esp_driver_ledc esp_driver_gpio esp_timer)
