#pragma once

#include <functional>

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
    NvsStore(const std::string& name)
        : name(name) {
    }

    bool contains(const std::string& key) {
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
                    LOGTW(Tag::NVS, "contains(%s) = failed to read: %s", key, esp_err_to_name(err));
                    break;
            }
            return err;
        }) == ESP_OK;
    }

    template <typename T>
    bool get(const std::string& key, T& value) {
        return get(key.c_str(), value);
    }

    template <typename T>
    bool get(const char* key, T& value) {
        return withPreferences(true, [&](nvs_handle_t handle) {
            size_t length = 0;
            esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
            if (err != ESP_OK) {
                LOGTV(Tag::NVS, "get(%s) = failed to read: %s", key, esp_err_to_name(err));
                return err;
            }

            char json[length];
            err = nvs_get_str(handle, key, json, &length);
            if (err != ESP_OK) {
                LOGTE(Tag::NVS, "get(%s) = failed to read: %s", key, esp_err_to_name(err));
                return err;
            }

            LOGTV(Tag::NVS, "get(%s) = %s", key, json);

            JsonDocument jsonDocument;
            DeserializationError jsonError = deserializeJson(jsonDocument, json);
            if (jsonError) {
                LOGTE(Tag::NVS, "get(%s) = invalid JSON: %s", key, jsonError.c_str());
                return ESP_FAIL;
            }

            value = jsonDocument.as<T>();
            return ESP_OK;
        }) == ESP_OK;
    }

    template <typename T>
    bool set(const std::string& key, const T& value) {
        return set(key.c_str(), value);
    }

    template <typename T>
    bool set(const char* key, const T& value) {
        return withPreferences(false, [&](nvs_handle_t handle) {
            JsonDocument jsonDocument;
            jsonDocument.set(value);
            std::string jsonString;
            serializeJson(jsonDocument, jsonString);

            LOGTV(Tag::NVS, "set(%s) = %s", key, jsonString.c_str());

            esp_err_t err = nvs_set_str(handle, key, jsonString.c_str());
            if (err != ESP_OK) {
                LOGTE(Tag::NVS, "set(%s) = failed to write: %s", key, esp_err_to_name(err));
                return err;
            }

            return nvs_commit(handle);
        }) == ESP_OK;
    }

    bool remove(const std::string& key) {
        return remove(key.c_str());
    }

    bool remove(const char* key) {
        return withPreferences(false, [&](nvs_handle_t handle) {
            LOGTV(Tag::NVS, "remove(%s)", key);
            esp_err_t err = nvs_erase_key(handle, key);
            if (err != ESP_OK) {
                LOGTE(Tag::NVS, "remove(%s) = cannot delete: %s", key, esp_err_to_name(err));
                return err;
            }

            return nvs_commit(handle);
        }) == ESP_OK;
    }

private:
    esp_err_t withPreferences(bool readOnly, std::function<esp_err_t(nvs_handle_t)> action) {
        Lock lock(preferencesMutex);
        LOGTV(Tag::NVS, "%s '%s'", readOnly ? "read" : "write", name.c_str());

        nvs_handle_t handle;
        esp_err_t err = nvs_open(name.c_str(), readOnly ? NVS_READONLY : NVS_READWRITE, &handle);
        switch (err) {
            case ESP_OK:
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                LOGTV(Tag::NVS, "namespace '%s' does not exist yet, nothing to read",
                    name.c_str());
                return ESP_ERR_NOT_FOUND;
                break;
            default:
                LOGTW(Tag::NVS, "failed to open NVS to %s '%s': %s",
                    readOnly ? "read" : "write", name.c_str(), esp_err_to_name(err));
                break;
        }

        esp_err_t result = action(handle);
        nvs_close(handle);

        LOGTV(Tag::NVS, "finished %s '%s', result: %s",
            readOnly ? "read" : "write", name.c_str(), esp_err_to_name(result));
        return result;
    }

    Mutex preferencesMutex;
    const std::string name;
};

}    // namespace farmhub::kernel
