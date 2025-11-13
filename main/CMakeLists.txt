idf_component_register(
    SRC_DIRS ${CMAKE_CURRENT_LIST_DIR}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/partitions.csv"
    PRIV_REQUIRES
        spiffs
        functions
        devices
)

# Do not fail on warnings in ESP-IDF
idf_build_set_property(COMPILE_OPTIONS "-Wno-error" APPEND)

component_compile_options(-Wall)
component_compile_options(-Wextra)
component_compile_options(-Wunused)
component_compile_options(-Wreturn-local-addr)
component_compile_options(-Werror)

if(IDF_TARGET STREQUAL "esp32s3")
    component_compile_options(-mtext-section-literals) # To fix 'literal target out of range' errors
endif()

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

if(NOT DEFINED FARMHUB_LOG_VERBOSE)
    set(FARMHUB_LOG_VERBOSE "$ENV{FARMHUB_LOG_VERBOSE}")
endif()

if(UD_DEBUG)
    component_compile_definitions(FARMHUB_DEBUG)
    component_compile_definitions(FARMHUB_LOG_VERBOSE="${FARMHUB_LOG_VERBOSE}")
    component_compile_definitions(DUMP_MQTT)
endif()

# Use `idf.py -DFSUPLOAD=1 flash` to upload the data partition
if(DEFINED FSUPLOAD AND FSUPLOAD)
    spiffs_create_partition_image(data ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
endif()
