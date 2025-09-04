#pragma once

#include <string.h>

#include <string>

#include <esp_log.h>

namespace farmhub::kernel {

enum class Level : uint8_t {
    None = 0,
    // Fatal = 1,
    Error = 2,
    Warning = 3,
    Info = 4,
    Debug = 5,
    Verbose = 6,
};

struct LogRecord {
    const Level level;
    const std::string message;
};

class Tag {
public:
    //
    // When adding elements here, make sure to also list them in Log::init()
    //
    static constexpr const char* FARMHUB = "farmhub";
    static constexpr const char* FS = "farmhub:fs";
    static constexpr const char* LEDC = "farmhub:ledc";
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
#define FARMHUB_LOG_LEVEL ESP_LOG_DEBUG
#else
#define FARMHUB_LOG_LEVEL ESP_LOG_INFO
#endif
#endif

#ifndef FARMHUB_LOG_VERBOSE
#define FARMHUB_LOG_VERBOSE ""
#endif

// helper: check if substring is in comma-separated list
inline bool loggingTagInList(const char* tag, const char* list) {
    if (!list)
        return false;
    const char* p = strstr(list, tag);
    while (p) {
        const char* after = p + strlen(tag);
        if ((p == list || p[-1] == ',') && (*after == '\0' || *after == ',')) {
            return true;
        }
        p = strstr(after, tag);
    }
    return false;
}

// LOGGING_TAG(varName, "tagname")
#define LOGGING_TAG(varName, name)                             \
    static constexpr const char* varName = "farmhub:" name;    \
    struct varName##_LoggerInit {                              \
        varName##_LoggerInit() {                               \
            esp_log_level_t lvl = FARMHUB_LOG_LEVEL;           \
            if (loggingTagInList(name, FARMHUB_LOG_VERBOSE)) { \
                lvl = ESP_LOG_VERBOSE;                         \
            }                                                  \
            esp_log_level_set(varName, lvl);                   \
        }                                                      \
    };                                                         \
    static const varName##_LoggerInit varName##_logger_init;

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
        Tag::LEDC,
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
