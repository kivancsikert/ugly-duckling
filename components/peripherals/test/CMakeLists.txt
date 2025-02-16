idf_component_register(SRC_DIRS "."
                    INCLUDE_DIRS "."
                    REQUIRES kernel catch2 bblanchon__arduinojson peripherals
                    WHOLE_ARCHIVE)
