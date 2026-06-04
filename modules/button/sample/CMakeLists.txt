cmake_minimum_required(VERSION 3.20.0)

# Register the button module so Zephyr picks up its Kconfig and CMakeLists.txt.
get_filename_component(ZEGO_BUTTON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZEGO_BUTTON_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zego_button_sample)

target_sources(app PRIVATE src/main.c)
