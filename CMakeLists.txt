cmake_minimum_required(VERSION 3.16.0)


# Add esp-idf-lib components
include(FetchContent)
FetchContent_Declare(
  espidflib
  GIT_REPOSITORY https://github.com/UncleRus/esp-idf-lib.git
  GIT_TAG "1abe2e5194b1a45c8878229ad893a058816cdd18"
)
FetchContent_MakeAvailable(espidflib)
set(EXTRA_COMPONENT_DIRS ${espidflib_SOURCE_DIR}/components ${CMAKE_SOURCE_DIR}/components)

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
        message(FATAL_ERROR "Error: Ugly Duckling ${UD_GEN} is only supported on ESP32-S2, currently IDF_TARGET = '$ENV{IDF_TARGET}'")
    endif()
elseif(UD_GEN STREQUAL "MK5" OR UD_GEN STREQUAL "MK6" OR UD_GEN STREQUAL "MK7")
    if (NOT "$ENV{IDF_TARGET}" STREQUAL "esp32s3")
        message(FATAL_ERROR "Error: Ugly Duckling ${UD_GEN} is only supported on ESP32-S3, currently IDF_TARGET = '$ENV{IDF_TARGET}'")
    endif()
elseif(UD_GEN STREQUAL "MK8")
    if (NOT "$ENV{IDF_TARGET}" STREQUAL "esp32c6")
        message(FATAL_ERROR "Error: Ugly Duckling ${UD_GEN} is only supported on ESP32-C6, currently IDF_TARGET = '$ENV{IDF_TARGET}'")
    endif()
else()
    message(FATAL_ERROR "Error: Unrecognized Ugly Duckling generation '${UD_GEN}'")
endif()

message("Building Ugly Duckling '${UD_GEN}'")

if(NOT DEFINED UD_DEBUG)
    set(UD_DEBUG "$ENV{UD_DEBUG}")
endif()
if(UD_DEBUG STREQUAL "")
    message("UD_DEBUG is not set, assuming 0.")
    set(UD_DEBUG 0)
endif()

# Make sure we reconfigure if UD_DEBUG changes
set_property(DIRECTORY PROPERTY UD_DEBUG_TRACKER "${UD_DEBUG}")

if (UD_DEBUG)
    message("Building with debug options")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.${UD_GEN_LOWER}.defaults;sdkconfig.debug.defaults")
    set(CMAKE_BUILD_TYPE Debug)
else()
    message("Building with release options")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.${UD_GEN_LOWER}.defaults")
    set(CMAKE_BUILD_TYPE Release)
endif()
add_link_options("-Wl,--gc-sections")

# Keep struct initializers simple
add_compile_options("-Wno-missing-field-initializers")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

project(ugly-duckling)

# Use `idf.py -DFSUPLOAD=1 flash` to upload the data partition
if(DEFINED FSUPLOAD AND FSUPLOAD)
    spiffs_create_partition_image(data ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
endif()
