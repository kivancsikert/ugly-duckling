idf_component_register(
    SRC_DIRS ${CMAKE_CURRENT_LIST_DIR}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/partitions.csv"
)

component_compile_options(-Wall)
component_compile_options(-Wextra)
component_compile_options(-Wunused)
component_compile_options(-Wreturn-local-addr)
component_compile_options(-Werror)

if(NOT DEFINED WOKWI)
    set(WOKWI "$ENV{WOKWI}")
endif()

# Make sure we reconfigure if parameters change
set_property(DIRECTORY PROPERTY WOKWI_TRACKER "${WOKWI}")

if(WOKWI)
    component_compile_definitions(WOKWI)
endif()

component_compile_definitions("${UD_GEN}")
component_compile_definitions(FARMHUB_REPORT_MEMORY)

if(UD_DEBUG)
    component_compile_definitions(FARMHUB_DEBUG)
    component_compile_definitions(DUMP_MQTT)
endif()
