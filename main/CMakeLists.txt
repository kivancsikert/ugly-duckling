# add_compile_definitions(MK4)
# add_compile_definitions(MK5)
# add_compile_definitions(MK6)
# add_compile_definitions(MK7)

# Get all source files from the main directory
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

# Specify include directories (no need to recurse into files)
idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/partitions.csv"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)

add_compile_definitions(MK7)
add_compile_definitions(FARMHUB_DEBUG)
add_compile_definitions(DUMP_MQTT)
add_compile_definitions(FARMHUB_REPORT_MEMORY)

# Set Git version
execute_process(
    COMMAND git describe --tags --dirty
    OUTPUT_VARIABLE FARMHUB_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_compile_definitions(FARMHUB_VERSION="${FARMHUB_VERSION}")
