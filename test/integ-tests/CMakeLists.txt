cmake_minimum_required(VERSION 3.16)

include(../../DeviceCommon.cmake)

#set(TEST_COMPONENTS "kernel" "peripherals" "devices" CACHE STRING "List of components to test")

project(ugly-duckling-integ-tests)

spiffs_create_partition_image(data ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
