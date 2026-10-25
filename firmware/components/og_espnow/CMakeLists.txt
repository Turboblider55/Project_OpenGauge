idf_component_register(SRCS "src/espnow_link.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES og_protocol
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer)
