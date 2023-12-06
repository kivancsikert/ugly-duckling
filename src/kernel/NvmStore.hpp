#pragma once

#include <functional>

#include <Preferences.h>

#include <ArduinoJson.h>

namespace farmhub { namespace kernel {

/**
 * @brief Thread safe NVM store for JSON serializable objects.
 */
class NvmStore {
public:
    NvmStore(const String& name)
        : name(name) {
    }

    template <typename T>
    bool get(const String& key, T& value, size_t bufferSize = DEFAULT_BUFFER_SIZE) {
        return get(key.c_str(), value, bufferSize);
    }

    template <typename T>
    bool get(const char* key, T& value, size_t bufferSize = DEFAULT_BUFFER_SIZE) {
        return withPreferences(true, [&]() {
            String jsonString = preferences.getString(key);
            if (jsonString.length() == 0) {
                return false;
            }
            // Serial.println("NVM: get(" + String(key) + ") = " + jsonString);
            DynamicJsonDocument jsonDocument(bufferSize);
            deserializeJson(jsonDocument, jsonString);
            if (jsonDocument.isNull()) {
                return false;
            }
            value = jsonDocument.as<T>();
            return true;
        });
    }

    template <typename T>
    bool set(const String& key, const T& value, size_t bufferSize = DEFAULT_BUFFER_SIZE) {
        return set(key.c_str(), value, bufferSize);
    }

    template <typename T>
    bool set(const char* key, const T& value, size_t bufferSize = DEFAULT_BUFFER_SIZE) {
        return withPreferences(false, [&]() {
            DynamicJsonDocument jsonDocument(bufferSize);
            jsonDocument.set(value);
            String jsonString;
            serializeJson(jsonDocument, jsonString);
            // Serial.println("NVM: set(" + String(key) + ") = " + jsonString);
            return preferences.putString(key, jsonString.c_str());
        });
    }

    bool remove(const String& key) {
        return remove(key.c_str());
    }

    bool remove(const char* key) {
        return withPreferences(false, [&]() {
            // Serial.println("NVM: remove(" + String(key) + ")");
            return preferences.remove(key);
        });
    }

private:
    bool withPreferences(bool readOnly, std::function<bool()> action) {
        xSemaphoreTake(preferencesMutex, portMAX_DELAY);
        preferences.begin(name.c_str(), readOnly);
        bool result = action();
        preferences.end();
        xSemaphoreGive(preferencesMutex);
        return result;
    }

    Preferences preferences;
    QueueHandle_t preferencesMutex { xSemaphoreCreateMutex() };
    const String name;

    static const size_t DEFAULT_BUFFER_SIZE = 2048;
};

}}    // namespace farmhub::kernel
