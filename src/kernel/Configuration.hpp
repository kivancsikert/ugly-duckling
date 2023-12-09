#pragma once

#include <ArduinoJson.h>
#include <functional>
#include <list>

#include <kernel/FileSystem.hpp>

using std::list;
using std::ref;
using std::reference_wrapper;

namespace farmhub { namespace kernel {

class ConfigurationEntry {
public:
    virtual void load(const JsonObject& json) = 0;
    virtual void reset() = 0;
    virtual void store(JsonObject& json, bool inlineDefaults) const = 0;
    virtual bool hasValue() const = 0;
};

class ConfigurationSection : public ConfigurationEntry {
public:
    void add(ConfigurationEntry& entry) {
        auto reference = std::ref(entry);
        entries.push_back(reference);
    }

    virtual void load(const JsonObject& json) override {
        for (auto& entry : entries) {
            entry.get().load(json);
        }
    }

    virtual void reset() override {
        for (auto& entry : entries) {
            entry.get().reset();
        }
    }

    virtual void store(JsonObject& json, bool inlineDefaults) const override {
        for (auto& entry : entries) {
            entry.get().store(json, inlineDefaults);
        }
    }

    virtual bool hasValue() const override {
        for (auto& entry : entries) {
            if (entry.get().hasValue()) {
                return true;
            }
        }
        return false;
    }

private:
    list<reference_wrapper<ConfigurationEntry>> entries;
};

class NamedConfigurationSection : public ConfigurationSection {
public:
    NamedConfigurationSection(ConfigurationSection* parent, const String& name)
        : name(name) {
        parent->add(*this);
    }

    void load(const JsonObject& json) override {
        if (json.containsKey(name)) {
            namePresentAtLoad = true;
            ConfigurationSection::load(json[name]);
        } else {
            reset();
        }
    }

    void store(JsonObject& json, bool inlineDefaults) const override {
        if (inlineDefaults || hasValue()) {
            auto section = json.createNestedObject(name);
            ConfigurationSection::store(section, inlineDefaults);
        }
    }

    bool hasValue() const override {
        return namePresentAtLoad || ConfigurationSection::hasValue();
    }

    void reset() override {
        namePresentAtLoad = false;
        ConfigurationSection::reset();
    }

private:
    const String name;
    bool namePresentAtLoad = false;
};

template <typename T>
class Property : public ConfigurationEntry {
public:
    Property(ConfigurationSection* parent, const String& name, const T& defaultValue, const bool secret = false)
        : name(name)
        , secret(secret)
        , defaultValue(defaultValue) {
        parent->add(*this);
    }

    void set(const T& value) {
        this->value = value;
        configured = true;
    }

    const T& get() const {
        return configured ? value : defaultValue;
    }

    void load(const JsonObject& json) override {
        if (json.containsKey(name)) {
            set(json[name].as<T>());
        } else {
            reset();
        }
    }

    bool hasValue() const override {
        return configured;
    }

    void reset() override {
        configured = false;
        value = T();
    }

    void store(JsonObject& json, bool inlineDefaults) const override {
        if (!configured && !inlineDefaults) {
            return;
        }
        if (secret) {
            json[name] = "********";
        } else {
            json[name] = get();
        }
    }

private:
    const String name;
    const bool secret;
    bool configured = false;
    T value;
    const T defaultValue;
};

class RawJsonEntry : public ConfigurationEntry {
public:
    RawJsonEntry(ConfigurationSection* parent, const String& name)
        : name(name) {
        parent->add(*this);
    }

    void load(const JsonObject& json) override {
        if (json.containsKey(name)) {
            value = json[name];
        } else {
            value.clear();
        }
    }

    void store(JsonObject& json, bool inlineDefaults) const override {
        json[name] = value;
    }

    void reset() override {
        value.clear();
    }

    JsonVariant get() {
        return value;
    }

    bool hasValue() const override {
        return !value.isNull();
    }

private:
    const String name;
    JsonVariant value;
};

class Configuration : protected ConfigurationSection {
public:
    Configuration(const String& name, size_t capacity = 2048)
        : name(name)
        , capacity(capacity) {
    }

    void reset() override {
        ConfigurationSection::reset();
    }

    virtual void update(const JsonObject& json) {
        load(json);
    }

    void onUpdate(const std::function<void()>& callback) {
        callbacks.push_back(callback);
    }

    virtual void store(JsonObject& json, bool inlineDefaults) const override {
        ConfigurationSection::store(json, inlineDefaults);
    }

protected:
    void load(const JsonObject& json) override {
        ConfigurationSection::load(json);

        // Print effective configuration
        DynamicJsonDocument prettyJson(capacity);
        auto prettyRoot = prettyJson.to<JsonObject>();
        ConfigurationSection::store(prettyRoot, true);
        Serial.println("Effective " + name + " configuration:");
        serializeJsonPretty(prettyJson, Serial);
        Serial.println();

        updated();
    }

    const String name;
    const size_t capacity;

private:
    void updated() {
        for (auto& callback : callbacks) {
            callback();
        }
    }

    std::list<std::function<void()>> callbacks;
};

class FileConfiguration : public Configuration {
public:
    FileConfiguration(FileSystem& fs, const String& name, const String& path, size_t capacity = 2048)
        : Configuration(name, capacity)
        , fs(fs)
        , path(path) {
    }

    void loadFromFileSystem() {
        DynamicJsonDocument json(capacity);
        if (!fs.exists(path)) {
            Serial.println("The " + name + " configuration file " + path + " was not found, falling back to defaults");
        } else {
            File file = fs.open(path, FILE_READ);
            if (!file) {
                throw "Cannot open config file " + path;
            }

            DeserializationError error = deserializeJson(json, file);
            file.close();
            if (error) {
                Serial.println(file.readString());
                throw "Cannot open config file " + path;
            }
        }
        load(json.as<JsonObject>());
    }

    void update(const JsonObject& json) override {
        Configuration::update(json);
        File file = fs.open(path, FILE_WRITE);
        if (!file) {
            throw "Cannot open config file " + path;
            return;
        }

        serializeJson(json, file);
        file.close();
    }

private:
    FileSystem& fs;

    const String path;
};

}}    // namespace farmhub::kernel

namespace std { namespace chrono {

using namespace std::chrono;

template <typename Duration>
bool convertToJson(const Duration& src, JsonVariant dst) {
    return dst.set(src.count());
}

template <typename Duration>
void convertFromJson(JsonVariantConst src, Duration& dst) {
    dst = Duration { src.as<uint64_t>() };
}

}}    // namespace std::chrono
