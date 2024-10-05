cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set up an alias from arduino to arduino-esp32
add_library(arduino INTERFACE)
target_link_libraries(arduino INTERFACE arduino-esp32)

list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/components)
list(APPEND EXTRA_COMPONENT_DIRS components)

add_compile_definitions(MK7)
add_compile_definitions(FARMHUB_DEBUG)

project(ugly-duckling)
