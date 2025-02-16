cmake_minimum_required(VERSION 3.16)

include(../../Common.cmake)

set(TEST_COMPONENTS "kernel" "peripherals" CACHE STRING "List of components to test")

project(ugly-duckling-unit-tests)
