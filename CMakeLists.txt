cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/components)
list(APPEND EXTRA_COMPONENT_DIRS components)

add_compile_definitions(MK7)
add_compile_definitions(FARMHUB_DEBUG)

execute_process(
    COMMAND git describe --tags --dirty
    OUTPUT_VARIABLE FARMHUB_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_compile_definitions(FARMHUB_VERSION="${FARMHUB_VERSION}")

project(ugly-duckling)
