#pragma once

#include <ArduinoJson.h>
#include <functional>
#include <list>

#include <kernel/FileSystem.hpp>
#include <kernel/Util.hpp>

using std::list;
using std::ref;
using std::reference_wrapper;

namespace farmhub { namespace kernel {

class JsonAsString {
public:
    JsonAsString() {
    }

    JsonAsString(const String& value)
        : value(value) {
    }

    JsonAsString(const JsonAsString& other)
        : value(other.value) {
    }

    const String& get() const {
        return value;
    }

    void set(const String& value) {
        this->value = value;
    }

private:
    String value;
};

bool convertToJson(const JsonAsString& src, JsonVariant dst) {
    // TODO How large should this be?
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, src.get());

    if (error) {
        // Handle the error, if JSON parsing fails
        return false;
    }

    dst.set(doc.as<JsonObject>());
    Serial.println(">>> Serialized --to-> JSON: " + src.get());
    return true;
}
bool convertFromJson(JsonVariantConst src, JsonAsString& dst) {
    String value;
    serializeJson(src, value);
    dst.set(value);
    Serial.println(">>> Serialized <-from-- JSON: " + dst.get());
    return true;
}

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
    Property(ConfigurationSection* parent, const String& name, const T& defaultValue = T(), const bool secret = false)
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

template <typename T>
class ObjectArrayProperty : public ConfigurationEntry {
public:
    ObjectArrayProperty(ConfigurationSection* parent, const String& name)
        : name(name) {
        parent->add(*this);
    }

    void set(const std::list<T>& entries) {
        this->entries = entries;
    }

    const std::list<T>& get() const {
        return entries;
    }

    void load(const JsonObject& json) override {
        reset();
        if (json.containsKey(name)) {
            auto jsonArray = json[name].as<JsonArray>();
            for (auto jsonEntry : jsonArray) {
                T entry;
                convertFromJson(jsonEntry, entry);
                entries.push_back(entry);
            }
        }
    }

    bool hasValue() const override {
        return !entries.empty();
    }

    void reset() override {
        entries.clear();
    }

    void store(JsonObject& json, bool inlineDefaults) const override {
        auto jsonArray = json.createNestedArray(name);
        for (auto& entry : entries) {
            JsonObject jsonEntry = jsonArray.createNestedObject();
            convertToJson(entry, jsonEntry);
        }
    }

private:
    const String name;
    std::list<T> entries;
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

    void load(const String& json) {
        DynamicJsonDocument jsonDocument(capacity);
        DeserializationError error = deserializeJson(jsonDocument, json);
        if (error) {
            throw "Cannot parse JSON configuration: " + String(error.c_str());
        }
        load(jsonDocument.as<JsonObject>());
    }

    virtual void update(const JsonObject& json) {
        load(json);
    }

    void onUpdate(const std::function<void(const JsonObject&)>& callback) {
        callbacks.push_back(callback);
    }

    virtual void store(JsonObject& json, bool inlineDefaults) const override {
        ConfigurationSection::store(json, inlineDefaults);
    }

    template <typename TDeviceConfiguration>
    static std::unique_ptr<TDeviceConfiguration> bindToFile(const FileSystem& fs, const String& path) {
        auto config = make_unique<TDeviceConfiguration>();
        DynamicJsonDocument json(config->capacity);
        if (!fs.exists(path)) {
            Serial.println("The configuration file " + path + " was not found, falling back to defaults");
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
        config->load(json.as<JsonObject>());
        config->onUpdate([&fs, path](const JsonObject& json) {
            File file = fs.open(path, FILE_WRITE);
            if (!file) {
                throw "Cannot open config file " + path;
            }

            serializeJson(json, file);
            file.close();
        });
        return config;
    }

    const String& getName() const {
        return name;
    }

protected:
    void load(const JsonObject& json) override {
        ConfigurationSection::load(json);

        // Print effective configuration
        DynamicJsonDocument prettyJson(capacity);
        auto prettyRoot = prettyJson.to<JsonObject>();
        store(prettyRoot, true);
        Serial.println("Effective " + name + " configuration:");
        serializeJsonPretty(prettyJson, Serial);
        Serial.println();

        updated(json);
    }

private:
    void updated(const JsonObject& json) {
        for (auto& callback : callbacks) {
            callback(json);
        }
    }

    const String name;
    const size_t capacity;
    std::list<std::function<void(const JsonObject&)>> callbacks;
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
