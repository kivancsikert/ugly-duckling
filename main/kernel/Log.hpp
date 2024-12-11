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

class Tag {
public:
    //
    // When adding elements here, make sure to also list them in Log::init()
    //
    static constexpr const char* FARMHUB = "farmhub";
    static constexpr const char* FS = "farmhub:fs";
    static constexpr const char* MDNS = "farmhub:mdns";
    static constexpr const char* MQTT = "farmhub:mqtt";
    static constexpr const char* NVS = "farmhub:nvs";
    static constexpr const char* PCNT = "farmhub:pcnt";
    static constexpr const char* PM = "farmhub:pm";
    static constexpr const char* RTC = "farmhub:rtc";
    static constexpr const char* WIFI = "farmhub:wifi";
};

#define LOGTE(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define LOGTW(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define LOGTI(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define LOGTD(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define LOGTV(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#define LOGE(format, ...) LOGTE(Tag::FARMHUB, format, ##__VA_ARGS__)
#define LOGW(format, ...) LOGTW(Tag::FARMHUB, format, ##__VA_ARGS__)
#define LOGI(format, ...) LOGTI(Tag::FARMHUB, format, ##__VA_ARGS__)
#define LOGD(format, ...) LOGTD(Tag::FARMHUB, format, ##__VA_ARGS__)
#define LOGV(format, ...) LOGTV(Tag::FARMHUB, format, ##__VA_ARGS__)

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
        Tag::FARMHUB,
        Tag::FS,
        Tag::MDNS,
        Tag::MQTT,
        Tag::NVS,
        Tag::PCNT,
        Tag::PM,
        Tag::RTC,
        Tag::WIFI,
    };
};

}    // namespace farmhub::kernel
