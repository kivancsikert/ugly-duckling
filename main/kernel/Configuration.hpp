#pragma once

#include <ArduinoJson.h>
#include <functional>
#include <list>

#include <kernel/FileSystem.hpp>

using std::list;
using std::ref;
using std::reference_wrapper;

namespace farmhub::kernel {

class ConfigurationException
    : public std::exception {
public:
    ConfigurationException(const String& message)
        : message("ConfigurationException: " + message) {
    }

    const char* what() const noexcept override {
        return message.c_str();
    }

    const String message;
};

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

    JsonAsString& operator=(const JsonAsString& other) = default;

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
    const String& stringValue = src.get();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, stringValue);

    if (error) {
        // Handle the error, if JSON parsing fails
        return false;
    }

    dst.set(doc.as<JsonObject>());
    return true;
}
bool convertFromJson(JsonVariantConst src, JsonAsString& dst) {
    String value;
    serializeJson(src, value);
    dst.set(value);
    return true;
}

class ConfigurationEntry {
public:
    void loadFromString(const String& json) {
        JsonDocument jsonDocument;
        DeserializationError error = deserializeJson(jsonDocument, json);
        if (error == DeserializationError::EmptyInput) {
            return;
        }
        if (error) {
            throw ConfigurationException("Cannot parse JSON configuration: " + String(error.c_str()) + json);
        }
        load(jsonDocument.as<JsonObject>());
    }

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

class EmptyConfiguration : public ConfigurationSection { };

template <typename TDelegate>
class NamedConfigurationEntry : public ConfigurationEntry {
public:
    template <typename... Args>
    NamedConfigurationEntry(ConfigurationSection* parent, const String& name, Args&&... args)
        : name(name)
        , delegate(std::forward<Args>(args)...) {
        parent->add(*this);
    }

    void load(const JsonObject& json) override {
        if (json[name].is<JsonVariant>()) {
            namePresentAtLoad = true;
            delegate.load(json[name]);
        } else {
            reset();
        }
    }

    void store(JsonObject& json, bool inlineDefaults) const override {
        if (inlineDefaults || hasValue()) {
            auto section = json[name].to<JsonObject>();
            delegate.store(section, inlineDefaults);
        }
    }

    bool hasValue() const override {
        return namePresentAtLoad || delegate.hasValue();
    }

    void reset() override {
        namePresentAtLoad = false;
        delegate.reset();
    }

    const TDelegate& get() const {
        return delegate;
    }

private:
    const String name;
    TDelegate delegate;
    bool namePresentAtLoad = false;
};

template <typename T>
class Property : public ConfigurationEntry {
public:
    Property(ConfigurationSection* parent, const String& name, const T& defaultValue = T(), const bool secret = false)
        : name(name)
        , secret(secret)
        , value(defaultValue)
        , defaultValue(defaultValue) {
        parent->add(*this);
    }

    const T& get() const {
        return configured ? value : defaultValue;
    }

    void load(const JsonObject& json) override {
        if (json[name].is<JsonVariant>()) {
            value = json[name].as<T>();
            configured = true;
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
class ArrayProperty : public ConfigurationEntry {
public:
    ArrayProperty(ConfigurationSection* parent, const String& name)
        : name(name) {
        parent->add(*this);
    }

    const std::list<T>& get() const {
        return entries;
    }

    void load(const JsonObject& json) override {
        reset();
        if (json[name].is<JsonArray>()) {
            auto jsonArray = json[name].as<JsonArray>();
            for (auto jsonEntry : jsonArray) {
                const T& entry = jsonEntry.as<T>();
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
        auto jsonArray = json[name].to<JsonArray>();
        for (auto& entry : entries) {
            jsonArray.add(entry);
        }
    }

private:
    const String name;
    std::list<T> entries;
};

template <typename TConfiguration>
class ConfigurationFile {
public:
    ConfigurationFile(const FileSystem& fs, const String& path)
        : path(path) {
        if (!fs.exists(path)) {
            Log.debug("The configuration file '%s' was not found, falling back to defaults",
                path.c_str());
        } else {
            File file = fs.open(path, FILE_READ);
            if (!file) {
                throw ConfigurationException("Cannot open config file " + path);
            }

            JsonDocument json;
            DeserializationError error = deserializeJson(json, file);
            file.close();
            if (error) {
                throw ConfigurationException("Cannot open config file " + path + " (" + String(error.c_str()) + ")");
            }
            update(json.as<JsonObject>());
            Log.info("Effective configuration for '%s': %s",
                path.c_str(), toString().c_str());
        }
        onUpdate([&fs, path](const JsonObject& json) {
            File file = fs.open(path, FILE_WRITE);
            if (!file) {
                throw ConfigurationException("Cannot open config file " + path);
            }

            serializeJson(json, file);
            file.close();
        });
    }

    void reset() {
        config.reset();
    }

    void update(const JsonObject& json) {
        config.load(json);

        for (auto& callback : callbacks) {
            callback(json);
        }
    }

    void onUpdate(const std::function<void(const JsonObject&)> callback) {
        callbacks.push_back(callback);
    }

    void store(JsonObject& json, bool inlineDefaults) const {
        config.store(json, inlineDefaults);
    }

    String toString(bool includeDefaults = true) {
        JsonDocument json;
        auto root = json.to<JsonObject>();
        store(root, includeDefaults);
        String jsonString;
        serializeJson(json, jsonString);
        return jsonString;
    }

    TConfiguration config;

private:
    const String path;
    std::list<std::function<void(const JsonObject&)>> callbacks;
};

}    // namespace farmhub::kernel

namespace std::chrono {

using namespace std::chrono;

template <typename Duration>
bool convertToJson(const Duration& src, JsonVariant dst) {
    return dst.set(src.count());
}

template <typename Duration>
void convertFromJson(JsonVariantConst src, Duration& dst) {
    dst = Duration { src.as<uint64_t>() };
}

}    // namespace std::chrono
