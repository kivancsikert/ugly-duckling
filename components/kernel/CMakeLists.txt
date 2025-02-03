FILE(GLOB_RECURSE lib_sources *.cpp **/*.cpp)

idf_component_register(
    SRCS ${lib_sources}
    INCLUDE_DIRS "."
)
