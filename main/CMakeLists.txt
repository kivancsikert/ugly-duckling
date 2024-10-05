# add_compile_definitions(MK4)
# add_compile_definitions(MK5)
# add_compile_definitions(MK6)
# add_compile_definitions(MK7)

# Get all source files from the main directory
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

# Specify include directories (no need to recurse into files)
idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/include"
    EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/partitions.csv"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
