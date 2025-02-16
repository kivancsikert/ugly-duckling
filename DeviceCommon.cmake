include(${CMAKE_CURRENT_LIST_DIR}/Common.cmake)

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

set(SDKCONFIG_FILES)
list(APPEND SDKCONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
list(APPEND SDKCONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.${UD_GEN_LOWER}.defaults")

# Check if UD_DEBUG is defined
if (UD_DEBUG)
    message("Building with debug options")
    # Add the debug-specific SDKCONFIG file to the list
    list(APPEND SDKCONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.debug.defaults")
    set(CMAKE_BUILD_TYPE Debug)
else()
    message("Building with release options")
    set(CMAKE_BUILD_TYPE Release)
endif()

list(JOIN SDKCONFIG_FILES ";" SDKCONFIG_DEFAULTS)

add_link_options("-Wl,--gc-sections")
