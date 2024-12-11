#pragma once

#include <functional>

#include <Arduino.h>

#include <nvs.h>
#include <nvs_flash.h>

#include <ArduinoJson.h>
#include <kernel/Concurrent.hpp>

namespace farmhub::kernel {

/**
 * @brief Thread-safe NVS store for JSON serializable objects.
 */
class NvsStore {
public:
    NvsStore(const String& name)
        : name(name) {
        // Initialize NVS
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            // NVS partition was truncated and needs to be erased
            // Retry nvs_flash_init
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK(err);
    }

    bool contains(const String& key) {
        return contains(key.c_str());
    }

    bool contains(const char* key) {
        return withPreferences(true, [&](nvs_handle_t handle) {
            size_t length = 0;
            esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
            switch (err) {
                case ESP_OK:
                case ESP_ERR_NVS_NOT_FOUND:
                    break;
                default:
                    LOGTW("nvs", "contains(%s) = failed to read: %s", key, esp_err_to_name(err));
                    break;
            }
            return err;
        }) == ESP_OK;
    }

    template <typename T>
    bool get(const String& key, T& value) {
        return get(key.c_str(), value);
    }

    template <typename T>
    bool get(const char* key, T& value) {
        return withPreferences(true, [&](nvs_handle_t handle) {
            size_t length = 0;
            esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
            if (err != ESP_OK) {
                LOGTV("nvs", "get(%s) = failed to read: %s", key, esp_err_to_name(err));
                return err;
            }

            char json[length];
            err = nvs_get_str(handle, key, json, &length);
            if (err != ESP_OK) {
                LOGTE("nvs", "get(%s) = failed to read: %s", key, esp_err_to_name(err));
                return err;
            }

            LOGTV("nvs", "get(%s) = %s", key, json);

            JsonDocument jsonDocument;
            DeserializationError jsonError = deserializeJson(jsonDocument, json);
            if (jsonError) {
                LOGTE("nvs", "get(%s) = invalid JSON: %s", key, jsonError.c_str());
                return ESP_FAIL;
            }

            value = jsonDocument.as<T>();
            return ESP_OK;
        }) == ESP_OK;
    }

    template <typename T>
    bool set(const String& key, const T& value) {
        return set(key.c_str(), value);
    }

    template <typename T>
    bool set(const char* key, const T& value) {
        return withPreferences(false, [&](nvs_handle_t handle) {
            JsonDocument jsonDocument;
            jsonDocument.set(value);
            String jsonString;
            serializeJson(jsonDocument, jsonString);

            LOGTV("nvs", "set(%s) = %s", key, jsonString.c_str());

            esp_err_t err = nvs_set_str(handle, key, jsonString.c_str());
            if (err != ESP_OK) {
                LOGTE("nvs", "set(%s) = failed to write: %s", key, esp_err_to_name(err));
                return err;
            }

            return nvs_commit(handle);
        }) == ESP_OK;
    }

    bool remove(const String& key) {
        return remove(key.c_str());
    }

    bool remove(const char* key) {
        return withPreferences(false, [&](nvs_handle_t handle) {
            LOGTV("nvs", "remove(%s)", key);
            esp_err_t err = nvs_erase_key(handle, key);
            if (err != ESP_OK) {
                LOGTE("nvs", "remove(%s) = cannot delete: %s", key, esp_err_to_name(err));
                return err;
            }

            return nvs_commit(handle);
        }) == ESP_OK;
    }

private:
    esp_err_t withPreferences(bool readOnly, std::function<esp_err_t(nvs_handle_t)> action) {
        Lock lock(preferencesMutex);
        LOGTV("nvs", "%s '%s'", readOnly ? "read" : "write", name.c_str());

        nvs_handle_t handle;
        esp_err_t err = nvs_open(name.c_str(), readOnly ? NVS_READONLY : NVS_READWRITE, &handle);
        if (err != ESP_OK) {
            LOGTE("nvs", "failed to %s '%s'", readOnly ? "read" : "write", name.c_str());
            return false;
        }

        esp_err_t result = action(handle);
        nvs_close(handle);

        LOGTV("nvs", "finished %s '%s', result: %s",
            readOnly ? "read" : "write", name.c_str(), esp_err_to_name(result));
        return result;
    }

    Mutex preferencesMutex;
    const String name;
};

}    // namespace farmhub::kernel
