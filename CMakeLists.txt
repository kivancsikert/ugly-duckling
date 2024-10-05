cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/components)
list(APPEND EXTRA_COMPONENT_DIRS components)

# Adds necessary definitions for compiling it using Serial symbol attached to the HW USB CDC port
add_compile_definitions(ARDUINO_USB_CDC_ON_BOOT=1)
add_compile_definitions(ARDUINO_USB_MODE=1)

project(ugly-duckling)
