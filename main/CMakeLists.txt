FILE(GLOB_RECURSE app_sources main.cpp **/*.cpp)

if(NOT DEFINED UD_DEBUG)
    set(UD_DEBUG "$ENV{UD_DEBUG}")
endif()
if(UD_DEBUG STREQUAL "")
    message("UD_DEBUG is not set, assuming 0.")
    set(UD_DEBUG 0)
endif()

if(NOT DEFINED WOKWI)
    set(WOKWI "$ENV{WOKWI}")
endif()

# Make sure we reconfigure if parameters change
set_property(DIRECTORY PROPERTY UD_DEBUG_TRACKER "${UD_DEBUG}")
set_property(DIRECTORY PROPERTY WOKWI_TRACKER "${WOKWI}")

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/partitions.csv"
)

add_compile_definitions("${UD_GEN}")
add_compile_definitions(FARMHUB_REPORT_MEMORY)

if(UD_DEBUG)
    add_compile_definitions(FARMHUB_DEBUG)
    add_compile_definitions(DUMP_MQTT)
else()
    add_compile_options(-O2)
endif()

if(WOKWI)
    add_compile_definitions(WOKWI)
endif()

# Set Git version
execute_process(
    COMMAND git describe --tags --dirty
    OUTPUT_VARIABLE FARMHUB_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_compile_definitions(FARMHUB_VERSION="${FARMHUB_VERSION}")
