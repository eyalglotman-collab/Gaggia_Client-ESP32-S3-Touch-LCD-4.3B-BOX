# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Eyal_espresso_client)

# Suppress noisy third-party header warnings when building managed LVGL sources.
idf_component_get_property(lvgl_component_lib lvgl__lvgl COMPONENT_LIB)
if(lvgl_component_lib)
    target_compile_options(${lvgl_component_lib} PRIVATE -Wno-unused-function)
endif()
