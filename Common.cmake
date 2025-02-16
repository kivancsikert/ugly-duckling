list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include(FetchContent)
FetchContent_Declare(
    esp_idf_lib
    GIT_REPOSITORY https://github.com/UncleRus/esp-idf-lib.git
    GIT_TAG "1abe2e5194b1a45c8878229ad893a058816cdd18"
)
FetchContent_MakeAvailable(esp_idf_lib)
list(APPEND EXTRA_COMPONENT_DIRS ${esp_idf_lib_SOURCE_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options("-Wno-missing-field-initializers")

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
