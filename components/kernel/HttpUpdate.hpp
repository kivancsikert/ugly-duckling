#pragma once

#include <string>

#include <ArduinoJson.h>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>

#include <FileSystem.hpp>
#include <Log.hpp>
#include <Watchdog.hpp>
#include <drivers/WiFiDriver.hpp>

namespace farmhub::kernel {

class HttpUpdater {
public:
    static void startUpdate(const std::string& url, std::shared_ptr<FileSystem> fs) {
        JsonDocument doc;
        doc["url"] = url;
        std::string content;
        serializeJson(doc, content);
        fs->writeAll(HttpUpdater::UPDATE_FILE, content);
        Task::run("update", 3072, [](Task& task) {
            LOGI("Restarting in 5 seconds to apply update");
            Task::delay(5s);
            esp_restart();
        });
    }

    static void performPendingHttpUpdateIfNecessary(std::shared_ptr<FileSystem> fs, std::shared_ptr<WiFiDriver> wifi, std::shared_ptr<Watchdog> watchdog) {
        // Do we need to update?
        if (!fs->exists(UPDATE_FILE)) {
            LOGV("No update file found, not updating");
            return;
        }

        auto contents = fs->readAll(UPDATE_FILE);
        if (!contents.has_value()) {
            LOGE("Failed to read update file");
            return;
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

        HttpUpdater updater(watchdog);
        updater.performPendingHttpUpdate(url, wifi);
    }

    static constexpr const char* UPDATE_FILE = "/update.json";

private:
    HttpUpdater(std::shared_ptr<Watchdog> watchdog)
        : watchdog(watchdog) {
    }

    void performPendingHttpUpdate(const std::string& url, std::shared_ptr<WiFiDriver> wifi) {
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
            .user_data = this,
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

    static esp_err_t httpEventHandler(esp_http_client_event_t* event) {
        auto updater = static_cast<HttpUpdater*>(event->user_data);
        return updater->handleEvent(event);
    }

    esp_err_t handleEvent(esp_http_client_event_t* event) {
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
            case HTTP_EVENT_ON_DATA: {
                LOGD("HTTP data: %d bytes", event->data_len);
                // Keep running while we are receiving data
                watchdog->restart();
                auto beforeBatch = downloaded / DOWNLOAD_NOTIFICATION_BATCH;
                downloaded += event->data_len;
                auto afterBatch = downloaded / DOWNLOAD_NOTIFICATION_BATCH;
                if (beforeBatch < afterBatch) {
                    LOGI("Downloaded %.02f KB", ((double) downloaded / 1024.0));
                }
                break;
            }
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

    const std::shared_ptr<Watchdog> watchdog;
    size_t downloaded = 0;

    static constexpr const size_t DOWNLOAD_NOTIFICATION_BATCH = 128 * 1024;
};

}    // namespace farmhub::kernel
