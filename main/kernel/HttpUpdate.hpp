#pragma once

#include <string>

#include <ArduinoJson.h>

#include <kernel/FileSystem.hpp>
#include <kernel/Log.hpp>
#include <kernel/PowerManager.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub::kernel {

static constexpr const char* UPDATE_FILE = "/update.json";

static esp_err_t httpEventHandler(esp_http_client_event_t* event) {
    switch (event->event_id) {
        case HTTP_EVENT_ERROR:
            LOGE("HTTP error, status code: %d",
                esp_http_client_get_status_code(event->client));
            break;
        case HTTP_EVENT_ON_CONNECTED:
            LOGD("HTTP connected");
            break;
        case HTTP_EVENT_HEADERS_SENT:
            LOGV("HTTP headers sent");
            break;
        case HTTP_EVENT_ON_HEADER:
            LOGV("HTTP header: %s: %s", event->header_key, event->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            LOGD("HTTP data: %d bytes", event->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            LOGD("HTTP finished");
            break;
        case HTTP_EVENT_DISCONNECTED:
            LOGD("HTTP disconnected");
            break;
        default:
            LOGW("Unknown HTTP event %d", event->event_id);
            break;
    }
    return ESP_OK;
}

static std::string handleHttpUpdate(FileSystem& fs, std::shared_ptr<WiFiDriver> wifi) {
    // Do we need to update?
    if (!fs.exists(UPDATE_FILE)) {
        // No update file, nothing to do
        return "";
    }

    // Don't sleep while we are performing the update
    PowerManagementLockGuard sleepLock(PowerManager::noLightSleep);

    auto contents = fs.readAll(UPDATE_FILE);
    if (!contents.has_value()) {
        return "Failed to read update file";
    }
    JsonDocument doc;
    auto error = deserializeJson(doc, contents.value());
    int deleteError = fs.remove(UPDATE_FILE);
    if (deleteError) {
        return "Failed to delete update file";
    }

    if (error) {
        return "Failed to parse update.json: " + std::string(error.c_str());
    }
    std::string url = doc["url"];
    if (url.empty()) {
        return "Command contains empty url";
    }

    LOGI("Updating from version %s via URL %s",
        farmhubVersion, url.c_str());

    LOGD("Waiting for network...");
    if (!wifi->getNetworkReady().awaitSet(15s)) {
        return "Network not ready, aborting update";
    }

    esp_http_client_config_t httpConfig = {
        .url = url.c_str(),
        .event_handler = httpEventHandler,
        // Additional buffers to fit headers
        // Updating directly via GitHub's release links requires these
        .buffer_size = 4 * 1024,
        .buffer_size_tx = 12 * 1024,
        .user_data = nullptr,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t otaConfig = {
        .http_config = &httpConfig,
    };
    esp_err_t ret = esp_https_ota(&otaConfig);
    if (ret == ESP_OK) {
        LOGI("Update succeeded, rebooting in 5 seconds...");
        Task::delay(5s);
        esp_restart();
    } else {
        LOGE("Update failed (%s), continuing with regular boot",
            esp_err_to_name(ret));
        return std::string("Firmware upgrade failed: ") + esp_err_to_name(ret);
    }
}

}    // namespace farmhub::kernel
