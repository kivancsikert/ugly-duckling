#include <UpdateFilter.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unordered_map>

using namespace cornucopia::ugly_duckling::kernel;

namespace {

JsonDocument makeConfigurations(const std::vector<std::pair<std::string, std::string>>& nameAndFingerprint) {
    JsonDocument doc;
    JsonObject configurations = doc.to<JsonObject>();
    for (const auto& [name, fingerprint] : nameAndFingerprint) {
        JsonObject entry = configurations[name].to<JsonObject>();
        entry["data"].to<JsonObject>();
        entry["fingerprint"] = fingerprint;
        entry["requestedAt"] = "2026-07-30T00:00:00Z";
    }
    return doc;
}

}    // namespace

TEST_CASE("filterUpdate keeps entries whose fingerprint differs from what's held") {
    auto doc = makeConfigurations({ { "valve1", "fp-new" } });
    auto result = filterUpdate(doc.as<JsonObjectConst>(), { { "valve1", "fp-old" } });

    REQUIRE(result.changed.size() == 1);
    REQUIRE(result.changed[0].name == "valve1");
    REQUIRE(result.changed[0].envelope.getFingerprint() == "fp-new");
    REQUIRE_FALSE(result.deviceChanged);
}

TEST_CASE("filterUpdate drops entries whose fingerprint already matches") {
    auto doc = makeConfigurations({ { "valve1", "fp-1" } });
    auto result = filterUpdate(doc.as<JsonObjectConst>(), { { "valve1", "fp-1" } });

    REQUIRE(result.changed.empty());
    REQUIRE_FALSE(result.deviceChanged);
}

TEST_CASE("filterUpdate ignores the whole message when nothing differs") {
    auto doc = makeConfigurations({ { "valve1", "fp-1" }, { "door1", "fp-2" } });
    auto result = filterUpdate(doc.as<JsonObjectConst>(), { { "valve1", "fp-1" }, { "door1", "fp-2" } });

    REQUIRE(result.changed.empty());
    REQUIRE_FALSE(result.deviceChanged);
}

TEST_CASE("filterUpdate keeps an entry with no held fingerprint (new function, or first-ever device config)") {
    auto doc = makeConfigurations({ { "valve1", "fp-1" } });
    auto result = filterUpdate(doc.as<JsonObjectConst>(), {});

    REQUIRE(result.changed.size() == 1);
    REQUIRE(result.changed[0].name == "valve1");
}

TEST_CASE("filterUpdate flags deviceChanged only when the 'device' entry itself survives the filter") {
    auto doc = makeConfigurations({ { "device", "fp-device-new" }, { "valve1", "fp-1" } });
    auto result = filterUpdate(doc.as<JsonObjectConst>(), { { "device", "fp-device-old" }, { "valve1", "fp-1" } });

    REQUIRE(result.changed.size() == 1);
    REQUIRE(result.changed[0].name == "device");
    REQUIRE(result.deviceChanged);
}

TEST_CASE("filterUpdate does not flag deviceChanged when only functions differ") {
    auto doc = makeConfigurations({ { "device", "fp-device" }, { "valve1", "fp-new" } });
    auto result = filterUpdate(doc.as<JsonObjectConst>(), { { "device", "fp-device" }, { "valve1", "fp-old" } });

    REQUIRE(result.changed.size() == 1);
    REQUIRE(result.changed[0].name == "valve1");
    REQUIRE_FALSE(result.deviceChanged);
}
