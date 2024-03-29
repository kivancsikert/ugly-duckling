#pragma once

#include <functional>

#include <Preferences.h>

#include <ArduinoJson.h>
#include <ArduinoLog.h>

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
                return false;
            }
            String jsonString = preferences.getString(key);
            Log.verboseln("NVS: get(%s) = %s",
                key, jsonString.c_str());
            JsonDocument jsonDocument;
            deserializeJson(jsonDocument, jsonString);
            if (jsonDocument.isNull()) {
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
            Log.verboseln("NVS: set(%s) = %s",
                key, jsonString.c_str());
            return preferences.putString(key, jsonString.c_str());
        });
    }

    bool remove(const String& key) {
        return remove(key.c_str());
    }

    bool remove(const char* key) {
        return withPreferences(false, [&]() {
            Log.verboseln("NVS: remove(%s)",
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
        preferences.begin(name.c_str(), readOnly);
        bool result = action();
        preferences.end();
        return result;
    }

    Preferences preferences;
    Mutex preferencesMutex;
    const String name;
};

}    // namespace farmhub::kernel
