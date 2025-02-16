cmake_minimum_required(VERSION 3.16.0)

include(DeviceCommon.cmake)

project(ugly-duckling)

# Use `idf.py -DFSUPLOAD=1 flash` to upload the data partition
if(DEFINED FSUPLOAD AND FSUPLOAD)
    spiffs_create_partition_image(data ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
endif()
