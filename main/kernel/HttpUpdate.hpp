#pragma once

#include <string>

#include <ArduinoJson.h>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>

#include <kernel/FileSystem.hpp>
#include <kernel/Log.hpp>
#include <kernel/Watchdog.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

namespace farmhub::kernel {

static constexpr const char* UPDATE_FILE = "/update.json";

static esp_err_t httpEventHandler(esp_http_client_event_t* event) {
    auto watchdog = static_cast<Watchdog*>(event->user_data);
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
            // Keep running while we are receiving data
            watchdog->restart();
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

static void performHttpUpdateIfNecessary(std::shared_ptr<FileSystem> fs, std::shared_ptr<WiFiDriver> wifi, std::shared_ptr<Watchdog> watchdog) {
    // Do we need to update?
    if (!fs->exists(UPDATE_FILE)) {
        LOGV("No update file found, not updating");
        return;
    }

    auto contents = fs->readAll(UPDATE_FILE);
    if (!contents.has_value()) {
        LOGE("Failed to read update file");
    }
    JsonDocument doc;
    auto error = deserializeJson(doc, contents.value());
    int deleteError = fs->remove(UPDATE_FILE);
    if (deleteError) {
        LOGE("Failed to delete update file");
        return;
    }

    if (error) {
        LOGE("Failed to parse update.json: %s", error.c_str());
        return;
    }
    std::string url = doc["url"];
    if (url.empty()) {
        LOGE("Update command contains empty url");
        return;
    }

    LOGI("Updating from version %s via URL %s",
        farmhubVersion, url.c_str());

    LOGD("Waiting for network...");
    if (!wifi->getNetworkReady().awaitSet(15s)) {
        LOGE("Network not ready, aborting update");
        return;
    }

    esp_http_client_config_t httpConfig = {
        .url = url.c_str(),
        .event_handler = httpEventHandler,
        // Additional buffers to fit headers
        // Updating directly via GitHub's release links requires these
        .buffer_size = 4 * 1024,
        .buffer_size_tx = 12 * 1024,
        .user_data = watchdog.get(),
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
        return;
    }
}

}    // namespace farmhub::kernel
