#include <catch2/catch_session.hpp>

#include <stdio.h>
#include <Log.hpp>

extern "C" void app_main(void) {
    int argc = 1;
    const char* argv[2] = {
        "target_test_main",
        NULL
    };

    farmhub::kernel::Log::init();

    auto result = Catch::Session().run(argc, argv);
    if (result != 0) {
        printf("Test failed with result %d\n", result);
    } else {
        printf("Test passed.\n");
    }
}
