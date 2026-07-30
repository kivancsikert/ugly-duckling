#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <string>

#include <functions/FunctionConfigTracker.hpp>

using namespace cornucopia::ugly_duckling::functions;

TEST_CASE("manifest is empty before anything is recorded") {
    FunctionConfigTracker tracker;
    REQUIRE(tracker.manifest().empty());
}

TEST_CASE("manifest reports fingerprints recorded at creation") {
    FunctionConfigTracker tracker;
    tracker.record("valve1", [](JsonObjectConst) {}, "fp-1");
    tracker.record("valve2", [](JsonObjectConst) {}, "fp-2");

    auto manifest = tracker.manifest();
    REQUIRE(manifest.size() == 2);
    REQUIRE(manifest.at("valve1") == "fp-1");
    REQUIRE(manifest.at("valve2") == "fp-2");
}

TEST_CASE("apply invokes the configureFn with the given data") {
    FunctionConfigTracker tracker;
    JsonObjectConst received;
    tracker.record("valve1", [&](JsonObjectConst data) {
        received = data;
    },
        "fp-1");

    JsonDocument body;
    body["openDuration"] = 30;
    tracker.apply("valve1", body.as<JsonObjectConst>(), "fp-2");

    REQUIRE(received["openDuration"].as<int>() == 30);
}

TEST_CASE("apply records the new fingerprint only after a successful configureFn") {
    FunctionConfigTracker tracker;
    tracker.record("valve1", [](JsonObjectConst) {}, "fp-1");

    JsonDocument body;
    tracker.apply("valve1", body.as<JsonObjectConst>(), "fp-2");

    REQUIRE(tracker.manifest().at("valve1") == "fp-2");
}

TEST_CASE("apply does not update the fingerprint when configureFn throws") {
    FunctionConfigTracker tracker;
    tracker.record("valve1", [](JsonObjectConst) {
        throw std::runtime_error("bad config");
    },
        "fp-1");

    JsonDocument body;
    REQUIRE_THROWS_WITH(tracker.apply("valve1", body.as<JsonObjectConst>(), "fp-2"), "bad config");

    REQUIRE(tracker.manifest().at("valve1") == "fp-1");
}

TEST_CASE("apply throws for an unknown function name") {
    FunctionConfigTracker tracker;

    JsonDocument body;
    REQUIRE_THROWS_MATCHES(
        tracker.apply("unknown", body.as<JsonObjectConst>(), "fp-1"),
        std::runtime_error,
        Catch::Matchers::Message("Cannot reconfigure unknown function 'unknown'"));
}

TEST_CASE("apply throws for a function recorded without a configureFn") {
    FunctionConfigTracker tracker;
    tracker.record("valve1", nullptr, "");

    JsonDocument body;
    REQUIRE_THROWS_MATCHES(
        tracker.apply("valve1", body.as<JsonObjectConst>(), "fp-1"),
        std::runtime_error,
        Catch::Matchers::Message("Function 'valve1' does not support configuration"));
}
