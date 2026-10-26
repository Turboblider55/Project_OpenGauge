idf_component_register(SRCS "src/pid_table.cpp" "src/isotp.cpp" "src/ecu_model.cpp" "src/obd_services.cpp" "src/obd_client.cpp" "src/pid_scheduler.cpp" "src/obd_detect.cpp" "src/telemetry_builder.cpp"
                       INCLUDE_DIRS "include" REQUIRES og_common og_protocol)
