#include <catch2/catch_test_macros.hpp>

#include <string>

#include <Configuration.hpp>

using namespace farmhub::kernel;

struct NestedConfig : ConfigurationSection {
    Property<int> intValue { this, "intValue" };
};

struct TestConfig : ConfigurationSection {
    Property<int> intValue { this, "intValue" };
    Property<std::string> stringValue { this, "stringValue" };
    Property<bool> boolValue { this, "boolValue" };
    NamedConfigurationEntry<NestedConfig> nested { this, "nested" };
};

std::string toString(const ConfigurationEntry& config) {
    JsonDocument json;
    auto root = json.to<JsonObject>();
    config.store(root);
    std::string jsonString;
    serializeJson(json, jsonString);
    return jsonString;
}

TEST_CASE("empty configuration is stored as empty JSON") {
    TestConfig config;
    REQUIRE(toString(config) == R"({})");
    REQUIRE(!config.intValue.hasValue());
    REQUIRE(config.intValue.get() == 0);
    REQUIRE(!config.stringValue.hasValue());
    REQUIRE(config.stringValue.get() == "");
    REQUIRE(!config.boolValue.hasValue());
    REQUIRE(config.boolValue.get() == false);
    REQUIRE(!config.nested.hasValue());
    REQUIRE(!config.nested.get()->intValue.hasValue());
    REQUIRE(config.nested.get()->intValue.get() == 0);
}

TEST_CASE("empty configuration can be loaded from empty JSON") {
    TestConfig config;
    config.loadFromString(R"({})");
    REQUIRE(!config.intValue.hasValue());
    REQUIRE(config.intValue.get() == 0);
    REQUIRE(!config.stringValue.hasValue());
    REQUIRE(config.stringValue.get() == "");
    REQUIRE(!config.boolValue.hasValue());
    REQUIRE(config.boolValue.get() == false);
    REQUIRE(!config.nested.hasValue());
    REQUIRE(!config.nested.get()->intValue.hasValue());
    REQUIRE(config.nested.get()->intValue.get() == 0);
}

TEST_CASE("configuration with values is loaded from JSON and is stored as JSON") {
    TestConfig config;
    config.loadFromString(R"({"intValue":42,"stringValue":"hello","boolValue":true,"nested":{"intValue":7}})");
    REQUIRE(config.intValue.hasValue());
    REQUIRE(config.intValue.get() == 42);
    REQUIRE(config.stringValue.hasValue());
    REQUIRE(config.stringValue.get() == "hello");
    REQUIRE(config.boolValue.hasValue());
    REQUIRE(config.boolValue.get() == true);
    REQUIRE(config.nested.hasValue());
    REQUIRE(config.nested.get()->intValue.hasValue());
    REQUIRE(config.nested.get()->intValue.get() == 7);
    REQUIRE(toString(config) == R"({"intValue":42,"stringValue":"hello","boolValue":true,"nested":{"intValue":7}})");
}

struct ConfigWithDefaults : public ConfigurationSection {
    Property<int> intValue { this, "intValue", 42 };
    Property<std::string> stringValue { this, "stringValue", "default" };
    Property<bool> boolValue { this, "boolValue", true };
    // TODO Add some nested configuration with defaults
};

TEST_CASE("configuration with default values loaded from empty JSON has default values") {
    ConfigWithDefaults config;
    REQUIRE(toString(config) == R"({})");
    REQUIRE(!config.intValue.hasValue());
    REQUIRE(config.intValue.get() == 42);
    REQUIRE(!config.stringValue.hasValue());
    REQUIRE(config.stringValue.get() == "default");
    REQUIRE(!config.boolValue.hasValue());
    REQUIRE(config.boolValue.get() == true);
}

TEST_CASE("configuration with default values loaded from non-empty JSON has actual values") {
    ConfigWithDefaults config;
    config.loadFromString(R"({"intValue": 100, "stringValue": "custom", "boolValue": false})");
    REQUIRE(toString(config) == R"({"intValue":100,"stringValue":"custom","boolValue":false})");
    REQUIRE(config.intValue.hasValue());
    REQUIRE(config.intValue.get() == 100);
    REQUIRE(config.stringValue.hasValue());
    REQUIRE(config.stringValue.get() == "custom");
    REQUIRE(config.boolValue.hasValue());
    REQUIRE(config.boolValue.get() == false);
}

// TODO Add some tests for invalid JSON handling
// TODO Add some tests for null values
// TODO Add some tests for properties not defined in the configuration type
