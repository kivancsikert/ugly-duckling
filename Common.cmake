list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include(FetchContent)
FetchContent_Declare(
    esp_idf_lib
    GIT_REPOSITORY https://github.com/esp-idf-lib/core.git
    GIT_TAG "254acb13520ca8c13f261b3676a52cd8d352f2f8"
)
FetchContent_MakeAvailable(esp_idf_lib)
list(APPEND EXTRA_COMPONENT_DIRS ${esp_idf_lib_SOURCE_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options("-Wno-missing-field-initializers")

# TODO Re-enable minimal build; with ESP-IDF v5.5 it fails
# # "Trim" the build. Include the minimal set of components, main, and anything it depends on.
# idf_build_set_property(MINIMAL_BUILD ON)
