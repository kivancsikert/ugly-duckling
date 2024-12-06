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

component_compile_definitions("${UD_GEN}")
component_compile_definitions(FARMHUB_REPORT_MEMORY)

if(UD_DEBUG)
    component_compile_definitions(FARMHUB_DEBUG)
    component_compile_definitions(DUMP_MQTT)
else()
    add_compile_options("-O2")
endif()

if(WOKWI)
    component_compile_definitions(WOKWI)
endif()
