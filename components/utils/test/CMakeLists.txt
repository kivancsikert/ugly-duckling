idf_component_register(SRC_DIRS "." "scheduling"
                    INCLUDE_DIRS "."
                    REQUIRES
                        utils
                        catch2
                        unit-test-support
                    EMBED_FILES
                        scheduling/idle-moisture-data.csv
                    WHOLE_ARCHIVE)
