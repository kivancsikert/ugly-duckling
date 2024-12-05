#pragma once

#include <thread>

#include <ArduinoJson.h>

#include <esp_log.h>

namespace farmhub::kernel {

enum class Level {
    None = 0,
    // Fatal = 1,
    Error = 2,
    Warning = 3,
    Info = 4,
    Debug = 5,
    Verbose = 6,
};

#define FARMHUB_LOG(level, format, ...) \
    ESP_LOG_LEVEL_LOCAL(level, "farmhub", format, ##__VA_ARGS__)

#define LOGE(format, ...) FARMHUB_LOG(ESP_LOG_ERROR, format, ##__VA_ARGS__)
#define LOGW(format, ...) FARMHUB_LOG(ESP_LOG_WARN, format, ##__VA_ARGS__)
#define LOGI(format, ...) FARMHUB_LOG(ESP_LOG_INFO, format, ##__VA_ARGS__)
#define LOGD(format, ...) FARMHUB_LOG(ESP_LOG_DEBUG, format, ##__VA_ARGS__)
#define LOGV(format, ...) FARMHUB_LOG(ESP_LOG_VERBOSE, format, ##__VA_ARGS__)

#ifndef FARMHUB_LOG_LEVEL
#ifdef FARMHUB_DEBUG
#define FARMHUB_LOG_LEVEL FARMHUB_LOG_LEVEL_DEBUG
#else
#define FARMHUB_LOG_LEVEL FARMHUB_LOG_LEVEL_INFO
#endif
#endif

bool convertToJson(const Level& src, JsonVariant dst) {
    return dst.set(static_cast<int>(src));
}
void convertFromJson(JsonVariantConst src, Level& dst) {
    dst = static_cast<Level>(src.as<int>());
}

static void initLogging() {
#ifdef FARMHUB_DEBUG
    esp_log_level_set("farmhub", ESP_LOG_DEBUG);
#else
    esp_log_level_set("farmhub", ESP_LOG_INFO);
#endif
}

}    // namespace farmhub::kernel
