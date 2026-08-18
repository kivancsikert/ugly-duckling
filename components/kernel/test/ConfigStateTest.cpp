#include <catch2/catch_test_macros.hpp>
#include <config/ConfigState.hpp>

using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::kernel::config;

TEST_CASE("ConfigSlot round-trips through JSON as 'a'/'b'") {
    JsonDocument docA;
    docA.set(ConfigSlot::A);
    REQUIRE(docA.as<std::string>() == "a");
    REQUIRE(docA.as<ConfigSlot>() == ConfigSlot::A);

    JsonDocument docB;
    docB.set(ConfigSlot::B);
    REQUIRE(docB.as<std::string>() == "b");
    REQUIRE(docB.as<ConfigSlot>() == ConfigSlot::B);
}

TEST_CASE("otherSlot() flips between A and B") {
    REQUIRE(otherSlot(ConfigSlot::A) == ConfigSlot::B);
    REQUIRE(otherSlot(ConfigSlot::B) == ConfigSlot::A);
}

TEST_CASE("RequestedConfigStatus round-trips through JSON") {
    JsonDocument doc;

    doc.set(RequestedConfigStatus::Pending);
    REQUIRE(doc.as<std::string>() == "pending");
    REQUIRE(doc.as<RequestedConfigStatus>() == RequestedConfigStatus::Pending);

    doc.set(RequestedConfigStatus::Attempted);
    REQUIRE(doc.as<std::string>() == "attempted");
    REQUIRE(doc.as<RequestedConfigStatus>() == RequestedConfigStatus::Attempted);

    doc.set(RequestedConfigStatus::Rejected);
    REQUIRE(doc.as<std::string>() == "rejected");
    REQUIRE(doc.as<RequestedConfigStatus>() == RequestedConfigStatus::Rejected);
}

TEST_CASE("RejectionCode round-trips through JSON as its google.rpc.Code integer") {
    JsonDocument doc;
    doc.set(RejectionCode::InvalidArgument);
    REQUIRE(doc.as<int>() == 3);
    REQUIRE(doc.as<RejectionCode>() == RejectionCode::InvalidArgument);

    doc.set(RejectionCode::Internal);
    REQUIRE(doc.as<int>() == 13);
    REQUIRE(doc.as<RejectionCode>() == RejectionCode::Internal);
}

TEST_CASE("RequestedConfig round-trips slot and status") {
    JsonDocument doc;
    doc.set(RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Attempted });

    REQUIRE(doc["slot"].as<std::string>() == "b");
    REQUIRE(doc["status"].as<std::string>() == "attempted");

    RequestedConfig roundTripped = doc.as<RequestedConfig>();
    REQUIRE(roundTripped.slot == ConfigSlot::B);
    REQUIRE(roundTripped.status == RequestedConfigStatus::Attempted);
}

TEST_CASE("default-constructed ConfigState has every field absent") {
    ConfigState state;
    REQUIRE_FALSE(state.confirmed.has_value());
    REQUIRE_FALSE(state.requested.has_value());
    REQUIRE_FALSE(state.rejection.has_value());
}

TEST_CASE("ConfigState round-trips with every field present") {
    ConfigState state {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Pending },
        .rejection = RejectionCode::ResourceExhausted,
    };

    JsonDocument doc;
    doc.set(state);

    REQUIRE(doc["confirmed"].as<std::string>() == "a");
    REQUIRE(doc["requested"]["slot"].as<std::string>() == "b");
    REQUIRE(doc["requested"]["status"].as<std::string>() == "pending");
    REQUIRE(doc["rejection"].as<int>() == 8);

    ConfigState roundTripped = doc.as<ConfigState>();
    REQUIRE(roundTripped.confirmed == ConfigSlot::A);
    REQUIRE(roundTripped.requested->slot == ConfigSlot::B);
    REQUIRE(roundTripped.requested->status == RequestedConfigStatus::Pending);
    REQUIRE(roundTripped.rejection == RejectionCode::ResourceExhausted);
}

TEST_CASE("ConfigState round-trips with only confirmed set") {
    ConfigState state { .confirmed = ConfigSlot::B };

    JsonDocument doc;
    doc.set(state);

    REQUIRE(doc["confirmed"].as<std::string>() == "b");
    REQUIRE_FALSE(doc["requested"].is<JsonObjectConst>());
    REQUIRE_FALSE(doc["rejection"].is<int>());

    ConfigState roundTripped = doc.as<ConfigState>();
    REQUIRE(roundTripped.confirmed == ConfigSlot::B);
    REQUIRE_FALSE(roundTripped.requested.has_value());
    REQUIRE_FALSE(roundTripped.rejection.has_value());
}

TEST_CASE("an empty JSON object parses to a ConfigState with every field absent") {
    JsonDocument doc;
    doc.to<JsonObject>();

    ConfigState state = doc.as<ConfigState>();
    REQUIRE_FALSE(state.confirmed.has_value());
    REQUIRE_FALSE(state.requested.has_value());
    REQUIRE_FALSE(state.rejection.has_value());
}
