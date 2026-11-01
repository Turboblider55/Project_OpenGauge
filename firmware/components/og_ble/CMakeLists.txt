# Bluetooth nélküli chipen (az ESP32-S2 ECU-szimulátoron) a komponens üres. A célchipet az
# idf_build_get_property adja, mert a Kconfig-változók ebben a fázisban még nem érhetők el.
idf_build_get_property(og_target IDF_TARGET)
if(og_target STREQUAL "esp32s2")
  idf_component_register()
else()
  idf_component_register(SRCS "src/ble_radio.cpp"
                         INCLUDE_DIRS "include"
                         PRIV_REQUIRES bt nvs_flash esp_timer)
endif()
