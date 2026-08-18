#include <FirmwareUpdateDecision.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cornucopia::ugly_duckling::kernel;

namespace {

JsonDocument makeFirmwareEntry(const char* version, const char* url) {
    JsonDocument doc;
    JsonObject firmware = doc.to<JsonObject>();
    firmware["version"] = version;
    firmware["url"] = url;
    firmware["requestedAt"] = "2026-08-05T12:00:00Z";
    return doc;
}

}    // namespace

TEST_CASE("parseFirmwareUpdate returns URL when version differs from current") {
    auto doc = makeFirmwareEntry("0.50.2", "https://r2.example.com/ugly-duckling-spinach-release.bin");
    auto result = parseFirmwareUpdate(doc.as<JsonObjectConst>(), "0.50.1");

    REQUIRE(result.has_value());
    REQUIRE(*result == "https://r2.example.com/ugly-duckling-spinach-release.bin");
}

TEST_CASE("parseFirmwareUpdate returns nullopt when version matches current") {
    auto doc = makeFirmwareEntry("0.50.2", "https://r2.example.com/ugly-duckling-spinach-release.bin");
    auto result = parseFirmwareUpdate(doc.as<JsonObjectConst>(), "0.50.2");

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseFirmwareUpdate returns nullopt when firmware entry is absent") {
    JsonDocument doc;
    auto result = parseFirmwareUpdate(doc.as<JsonObjectConst>(), "0.50.1");

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseFirmwareUpdate returns nullopt when url is missing") {
    JsonDocument doc;
    JsonObject firmware = doc.to<JsonObject>();
    firmware["version"] = "0.50.2";
    auto result = parseFirmwareUpdate(doc.as<JsonObjectConst>(), "0.50.1");

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseFirmwareUpdate returns nullopt when url is empty") {
    auto doc = makeFirmwareEntry("0.50.2", "");
    auto result = parseFirmwareUpdate(doc.as<JsonObjectConst>(), "0.50.1");

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseFirmwareUpdate returns nullopt when version is missing") {
    JsonDocument doc;
    JsonObject firmware = doc.to<JsonObject>();
    firmware["url"] = "https://r2.example.com/firmware.bin";
    auto result = parseFirmwareUpdate(doc.as<JsonObjectConst>(), "0.50.1");

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseFirmwareUpdate returns nullopt when version is empty") {
    auto doc = makeFirmwareEntry("", "https://r2.example.com/firmware.bin");
    auto result = parseFirmwareUpdate(doc.as<JsonObjectConst>(), "0.50.1");

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseFirmwareUpdate ignores requestedAt without failing") {
    auto doc = makeFirmwareEntry("0.50.3", "https://r2.example.com/firmware.bin");
    auto result = parseFirmwareUpdate(doc.as<JsonObjectConst>(), "0.50.1");

    REQUIRE(result.has_value());
    REQUIRE(*result == "https://r2.example.com/firmware.bin");
}
