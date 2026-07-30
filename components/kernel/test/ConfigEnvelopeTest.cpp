#include <catch2/catch_test_macros.hpp>

#include <string>

#include <ConfigEnvelope.hpp>

using namespace cornucopia::ugly_duckling::kernel;

TEST_CASE("envelope round-trips data, fingerprint, and requestedAt through JSON") {
    JsonDocument body;
    body["peripherals"] = "[]";
    body["publishInterval"] = 60;

    ConfigEnvelope envelope(body.as<JsonVariantConst>(), "abc123", "2026-07-30T12:34:56Z");

    JsonDocument serialized;
    serialized.set(envelope);

    REQUIRE(serialized["fingerprint"].as<std::string>() == "abc123");
    REQUIRE(serialized["requestedAt"].as<std::string>() == "2026-07-30T12:34:56Z");
    REQUIRE(serialized["data"]["peripherals"].as<std::string>() == "[]");
    REQUIRE(serialized["data"]["publishInterval"].as<int>() == 60);

    ConfigEnvelope roundTripped = serialized.as<ConfigEnvelope>();
    REQUIRE(roundTripped.getFingerprint() == "abc123");
    REQUIRE(roundTripped.getRequestedAt() == "2026-07-30T12:34:56Z");
    REQUIRE(roundTripped.getData()["peripherals"].as<std::string>() == "[]");
    REQUIRE(roundTripped.getData()["publishInterval"].as<int>() == 60);
}

TEST_CASE("envelope preserves nested data verbatim, including arrays") {
    JsonDocument body;
    JsonArray functions = body["functions"].to<JsonArray>();
    functions.add("valve1");
    functions.add("valve2");
    body["nested"]["flag"] = true;

    ConfigEnvelope envelope(body.as<JsonVariantConst>(), "fp", "2026-07-30T00:00:00Z");

    JsonDocument serialized;
    serialized.set(envelope);
    ConfigEnvelope roundTripped = serialized.as<ConfigEnvelope>();

    JsonArrayConst roundTrippedFunctions = roundTripped.getData()["functions"];
    REQUIRE(roundTrippedFunctions.size() == 2);
    REQUIRE(roundTrippedFunctions[0].as<std::string>() == "valve1");
    REQUIRE(roundTrippedFunctions[1].as<std::string>() == "valve2");
    REQUIRE(roundTripped.getData()["nested"]["flag"].as<bool>() == true);
}

TEST_CASE("default-constructed envelope has empty fingerprint and requestedAt") {
    ConfigEnvelope envelope;
    REQUIRE(envelope.getFingerprint() == "");
    REQUIRE(envelope.getRequestedAt() == "");
    REQUIRE(envelope.getData().isNull());
}
