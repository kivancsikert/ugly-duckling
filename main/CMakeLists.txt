idf_component_register(
    SRC_DIRS ${CMAKE_CURRENT_LIST_DIR}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/partitions.csv"
    PRIV_REQUIRES
        functions
        devices
)

# Generate the NVS config partition. Use `idf.py flash` to upload it along
# with the firmware, or `idf.py app-flash` to upload only the app.
set(CONFIG_NVS_BIN "${CMAKE_BINARY_DIR}/config.bin")
# NVS partition size (must match partitions.csv)
set(CONFIG_NVS_SIZE "0x40000")

# Path to the network config JSON seeded into the NVS partition. Override with
# `-DUD_NETWORK_CONFIG=/path/to/network-config.json` to seed a different config
# (e.g. one with real credentials) without touching the tracked default. A
# relative path is resolved against the project root, not the current working
# directory (which under `idf.py` is the build directory) — so
# `-DUD_NETWORK_CONFIG=config/network-config.json` works as expected.
#
# Deliberately typed STRING, not FILEPATH: CMake silently rewrites -D-supplied
# FILEPATH/PATH cache values into absolute paths resolved against the cwd at
# cmake-invocation time (the build dir under `idf.py`) before this file even
# runs, which would defeat the get_filename_component() resolution below.
set(UD_NETWORK_CONFIG "${CMAKE_SOURCE_DIR}/config/network-config.json" CACHE STRING
    "Path to the network-config.json seeded into the NVS partition")
get_filename_component(UD_NETWORK_CONFIG_ABS "${UD_NETWORK_CONFIG}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")

set(GEN_CONFIG_NVS_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/gen_config_nvs.py")
add_custom_command(
    OUTPUT ${CONFIG_NVS_BIN}
    COMMAND ${PYTHON} ${GEN_CONFIG_NVS_SCRIPT}
        ${UD_NETWORK_CONFIG_ABS}
        ${CONFIG_NVS_BIN}
        ${CONFIG_NVS_SIZE}
    DEPENDS
        ${UD_NETWORK_CONFIG_ABS}
        ${GEN_CONFIG_NVS_SCRIPT}
    COMMENT "Generating NVS config partition from ${UD_NETWORK_CONFIG_ABS}"
)
add_custom_target(config_nvs_image ALL DEPENDS ${CONFIG_NVS_BIN})

esptool_py_flash_to_partition(flash "nvs" ${CONFIG_NVS_BIN})
