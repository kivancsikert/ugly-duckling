idf_component_register(
    SRC_DIRS ${CMAKE_CURRENT_LIST_DIR}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/partitions.csv"
)

# Do not fail on warnings in ESP-IDF
idf_build_set_property(COMPILE_OPTIONS "-Wno-error" APPEND)

component_compile_options(-Wall)
component_compile_options(-Wextra)
component_compile_options(-Wunused)
component_compile_options(-Wreturn-local-addr)
component_compile_options(-Werror)

if(NOT DEFINED WOKWI)
    set(WOKWI "$ENV{WOKWI}")
endif()

if(NOT DEFINED WOKWI_MQTT_HOST)
    set(WOKWI_MQTT_HOST "$ENV{WOKWI_MQTT_HOST}")
endif()

# Make sure we reconfigure if parameters change
set_property(DIRECTORY PROPERTY WOKWI_TRACKER "${WOKWI} ${WOKWI_MQTT_HOST}")

if(WOKWI)
    component_compile_definitions(WOKWI)
    if (WOKWI_MQTT_HOST)
        component_compile_definitions(WOKWI_MQTT_HOST="${WOKWI_MQTT_HOST}")
    endif()
endif()

component_compile_definitions("${UD_GEN}")
component_compile_definitions(FARMHUB_REPORT_MEMORY)

if(UD_DEBUG)
    component_compile_definitions(FARMHUB_DEBUG)
    component_compile_definitions(DUMP_MQTT)
endif()
