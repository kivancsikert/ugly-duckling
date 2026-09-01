idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES
        app_update
        test-support

        kernel-test
        devices-test
    WHOLE_ARCHIVE
)
