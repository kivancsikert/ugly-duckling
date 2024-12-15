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

#define LOGE(format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, "farmhub", format, ##__VA_ARGS__)
#define LOGW(format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN, "farmhub", format, ##__VA_ARGS__)
#define LOGI(format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO, "farmhub", format, ##__VA_ARGS__)
#define LOGD(format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, "farmhub", format, ##__VA_ARGS__)
#define LOGV(format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, "farmhub", format, ##__VA_ARGS__)

#define LOGTE(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, "farmhub:" tag, format, ##__VA_ARGS__)
#define LOGTW(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN, "farmhub:" tag, format, ##__VA_ARGS__)
#define LOGTI(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO, "farmhub:" tag, format, ##__VA_ARGS__)
#define LOGTD(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, "farmhub:" tag, format, ##__VA_ARGS__)
#define LOGTV(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, "farmhub:" tag, format, ##__VA_ARGS__)

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

class Log {
public:
    static constexpr const char* FARMHUB = "farmhub";
    static constexpr const char* MDNS = "farmhub:mdns";
    static constexpr const char* MQTT = "farmhub:mqtt";
    static constexpr const char* PM = "farmhub:pm";
    static constexpr const char* NVS = "farmhub:nvs";
    static constexpr const char* RTC = "farmhub:rtc";
    static constexpr const char* WIFI = "farmhub:wifi";

    static void init() {
#ifdef FARMHUB_DEBUG
        // Reset ANSI colors
        printf("\033[0m");
#endif

        for (const char* tag : TAGS) {
#ifdef FARMHUB_DEBUG
            esp_log_level_set(tag, ESP_LOG_DEBUG);
#else
            esp_log_level_set(tag, ESP_LOG_INFO);
#endif
        }
    }

private:
    static constexpr const char* TAGS[] = {
        FARMHUB,
        MDNS,
        MQTT,
        PM,
        NVS,
        RTC,
        WIFI,
    };
};

}    // namespace farmhub::kernel
