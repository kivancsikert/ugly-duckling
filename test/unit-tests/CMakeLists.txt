cmake_minimum_required(VERSION 3.16)

include(../../Common.cmake)

# Set the components to include the tests for.
# This can be overridden from CMake cache:
# - when invoking CMake directly: cmake -D TEST_COMPONENTS="xxxxx" ..
# - when using idf.py: idf.py -T xxxxx build
#
set(TEST_COMPONENTS "kernel" "peripherals" CACHE STRING "List of components to test")

project(ugly-duckling-test)
