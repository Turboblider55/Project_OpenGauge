idf_component_register(SRCS "src/twai_bus.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES og_obd
                       PRIV_REQUIRES driver esp_timer)
