#include <iostream>
#include <stdio.h>
#include <string_view>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

inline std::string_view filenameFromPath(const char* path) {
    if (!path) {
        return {};
    }

    std::string_view sv { path };
    size_t pos = sv.find_last_of("/\\");
    return (pos == std::string_view::npos) ? sv : sv.substr(pos + 1);
}

struct testRunListener : Catch::EventListenerBase {
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseStarting(Catch::TestCaseInfo const& testInfo) override {
        auto file = filenameFromPath(testInfo.lineInfo.file);
        if (file != currentTestFile) {
            std::cout << "\n";
            std::cout << std::setfill('-') << std::setw(file.length() + 3) << "" << "\n";
            std::cout << file << "\n";
            std::cout << std::setfill('-') << std::setw(file.length() + 3) << "" << "\n";
            currentTestFile = file;
        }
        std::cout << "\n>>> " << testInfo.name << "\n";
    }

private:
    std::string currentTestFile;
};

CATCH_REGISTER_LISTENER(testRunListener)

extern "C" void app_main(void) {
    const char* argv[] = {
        "target_test_main",
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    Catch::Session session;
    session.configData().rngSeed = 12345;
    auto result = session.run(argc, argv);
    if (result != 0) {
        printf("Test failed with result %d\n", result);
    } else {
        printf("Test passed.\n");
    }
}
