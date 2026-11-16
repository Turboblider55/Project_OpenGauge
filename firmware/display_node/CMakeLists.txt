cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components ${CMAKE_CURRENT_LIST_DIR}/components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Mérési mód: az LVGL-profilozó csatlakozó-fejléce (CONFIG_LV_PROFILER_INCLUDE) minden komponens fordításához kell.
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/main/prof" APPEND)
project(og_display_node)
