#pragma once

#include <functional>

#include <Preferences.h>

#include <ArduinoJson.h>

#include <kernel/Concurrent.hpp>

namespace farmhub::kernel {

/**
 * @brief Thread safe NVS store for JSON serializable objects.
 */
class NvsStore {
public:
    NvsStore(const String& name)
        : name(name) {
    }

    bool contains(const String& key) {
        return contains(key.c_str());
    }

    bool contains(const char* key) {
        return withPreferences(true, [&]() {
            return preferences.isKey(key);
        });
    }

    template <typename T>
    bool get(const String& key, T& value) {
        return get(key.c_str(), value);
    }

    template <typename T>
    bool get(const char* key, T& value) {
        return withPreferences(true, [&]() {
            if (!preferences.isKey(key)) {
                LOGTV("nvs", "get(%s) = not found",
                    key);
                return false;
            }
            String jsonString = preferences.getString(key);
            LOGTV("nvs", "get(%s) = %s",
                key, jsonString.c_str());
            JsonDocument jsonDocument;
            deserializeJson(jsonDocument, jsonString);
            if (jsonDocument.isNull()) {
                LOGTE("nvs", "get(%s) = invalid JSON",
                    key);
                return false;
            }
            value = jsonDocument.as<T>();
            return true;
        });
    }

    template <typename T>
    bool set(const String& key, const T& value) {
        return set(key.c_str(), value);
    }

    template <typename T>
    bool set(const char* key, const T& value) {
        return withPreferences(false, [&]() {
            JsonDocument jsonDocument;
            jsonDocument.set(value);
            String jsonString;
            serializeJson(jsonDocument, jsonString);
            LOGTV("nvs", "set(%s) = %s",
                key, jsonString.c_str());
            return preferences.putString(key, jsonString.c_str());
        });
    }

    bool remove(const String& key) {
        return remove(key.c_str());
    }

    bool remove(const char* key) {
        return withPreferences(false, [&]() {
            LOGTV("nvs", "remove(%s)",
                key);
            if (preferences.isKey(key)) {
                return preferences.remove(key);
            } else {
                return false;
            }
        });
    }

private:
    bool withPreferences(bool readOnly, std::function<bool()> action) {
        Lock lock(preferencesMutex);
        LOGTV("nvs", "%s '%s'",
            readOnly ? "read" : "write", name.c_str());
        if (!preferences.begin(name.c_str(), readOnly)) {
            LOGTE("nvs", "failed to %s '%s'",
                readOnly ? "read" : "write", name.c_str());
            return false;
        }
        bool result = action();
        preferences.end();
        LOGTV("nvs", "finished %s '%s', result: %s",
            readOnly ? "read" : "write", name.c_str(), result ? "true" : "false");
        return result;
    }

    Preferences preferences;
    Mutex preferencesMutex;
    const String name;
};

}    // namespace farmhub::kernel
