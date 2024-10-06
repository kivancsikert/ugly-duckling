cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/components)
list(APPEND EXTRA_COMPONENT_DIRS components)

# Adds necessary definitions for compiling it using Serial symbol attached to the HW USB CDC port
add_compile_definitions(ARDUINO_USB_CDC_ON_BOOT=1)
add_compile_definitions(ARDUINO_USB_MODE=1)

if(NOT DEFINED UD_GEN)
    set(UD_GEN "$ENV{UD_GEN}")
endif()
if(UD_GEN STREQUAL "")
    message("UD_GEN is not set, assuming MK7.")
    set(UD_GEN MK7)
endif()
message("Building for Ugly Duckling '${UD_GEN}'")

if(UD_GEN STREQUAL MK4)
    add_compile_definitions(Serial=Serial0)
endif()

project(ugly-duckling)

# Use `idf.py -DFSUPLOAD=1 flash` to upload the data partition
if(DEFINED FSUPLOAD AND FSUPLOAD)
    spiffs_create_partition_image(data ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
endif()
