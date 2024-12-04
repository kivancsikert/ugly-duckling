cmake_minimum_required(VERSION 3.16.0)

if(NOT DEFINED UD_GEN)
    set(UD_GEN "$ENV{UD_GEN}")
endif()
if(UD_GEN STREQUAL "")
    message("UD_GEN is not set, assuming MK7.")
    set(UD_GEN MK7)
endif()
string(TOLOWER "${UD_GEN}" UD_GEN_LOWER)

# Make sure we reconfigure if UD_GEN changes
set_property(GLOBAL PROPERTY UD_GEN_TRACKER "${UD_GEN}")

if(UD_GEN STREQUAL "MK4")
    if (NOT "$ENV{IDF_TARGET}" STREQUAL "esp32s2")
        message(FATAL_ERROR "Error: Ugly Duckling MK4 is only supported on ESP32-S2, currently IDF_TARGET = '$ENV{IDF_TARGET}'")
    endif()
elseif(UD_GEN STREQUAL "MK5" OR UD_GEN STREQUAL "MK6" OR UD_GEN STREQUAL "MK7")
    if (NOT "$ENV{IDF_TARGET}" STREQUAL "esp32s3")
        message(FATAL_ERROR "Error: Ugly Duckling ${UD_GEN} is only supported on ESP32-S3, currently IDF_TARGET = '$ENV{IDF_TARGET}'")
    endif()
else()
    message(FATAL_ERROR "Error: Unrecognized Ugly Duckling generation '${UD_GEN}'")
endif()

message("Building Ugly Duckling '${UD_GEN}'")

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.${UD_GEN_LOWER}.defaults")
# set(SDKCONFIG "sdkconfig.${UD_GEN_LOWER}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(DEPENDENCIES_LOCK "dependencies.lock")

# list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/components)
list(APPEND EXTRA_COMPONENT_DIRS components)

# Adds necessary definitions for compiling it using Serial symbol attached to the HW USB CDC port
add_compile_definitions(ARDUINO_USB_CDC_ON_BOOT=1)
add_compile_definitions(ARDUINO_USB_MODE=1)

if(UD_GEN STREQUAL MK4)
    add_compile_definitions(HWCDCSerial=Serial0)
endif()

# Keep struct initializers simple
add_compile_options("-Wno-missing-field-initializers")

# TODO Make this depend on some flag
set(CMAKE_BUILD_TYPE Debug)

project(ugly-duckling)

# Use `idf.py -DFSUPLOAD=1 flash` to upload the data partition
if(DEFINED FSUPLOAD AND FSUPLOAD)
    spiffs_create_partition_image(data ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
endif()
