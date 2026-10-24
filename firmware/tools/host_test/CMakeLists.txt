cmake_minimum_required(VERSION 3.16)
project(og_host_test CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# MSVC nem ismeri a GCC/Clang figyelmeztetés-kapcsolóit; ott /W4, és UTF-8 forrás (magyar kommentek).
if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
enable_testing()
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
include_directories(${ROOT}/components/og_protocol/include ${ROOT}/components/og_obd/include
                    ${ROOT}/components/og_common/include ${ROOT}/display_node/components/disp_smooth/include)
add_executable(test_protocol test_protocol.cpp ${ROOT}/components/og_protocol/src/og_codec.cpp)
add_executable(test_pid test_pid.cpp ${ROOT}/components/og_obd/src/pid_table.cpp)
set(OBD_SRC ${ROOT}/components/og_obd/src/pid_table.cpp ${ROOT}/components/og_obd/src/isotp.cpp
            ${ROOT}/components/og_obd/src/ecu_model.cpp ${ROOT}/components/og_obd/src/obd_services.cpp
            ${ROOT}/components/og_obd/src/obd_client.cpp ${ROOT}/components/og_obd/src/pid_scheduler.cpp
            ${ROOT}/components/og_obd/src/obd_detect.cpp)
add_executable(test_isotp test_isotp.cpp ${OBD_SRC})
add_executable(test_obd test_obd.cpp ${OBD_SRC})
add_executable(test_smoother test_smoother.cpp)
add_test(NAME protocol COMMAND test_protocol)
add_test(NAME pid COMMAND test_pid)
add_test(NAME smoother COMMAND test_smoother)
add_test(NAME isotp COMMAND test_isotp)
add_test(NAME obd COMMAND test_obd)
