#pragma once

#define LOGGING_TAG(name, ...) \
    static constexpr const char* TAG = name;

#define LOG(fmt, ...)                         \
    do {                                      \
        std::printf(fmt "\n", ##__VA_ARGS__); \
    } while (0)

#define LOGV(fmt, ...) LOG("VERBOSE - " fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LOG("DEBUG   - " fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG("INFO    - " fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG("WARNING - " fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG("ERROR   - " fmt, ##__VA_ARGS__)

#define LOGTV(tag, fmt, ...) LOGV("[" tag "] " fmt, ##__VA_ARGS__)
#define LOGTD(tag, fmt, ...) LOGD("[" tag "] " fmt, ##__VA_ARGS__)
#define LOGTI(tag, fmt, ...) LOGI("[" tag "] " fmt, ##__VA_ARGS__)
#define LOGTW(tag, fmt, ...) LOGW("[" tag "] " fmt, ##__VA_ARGS__)
#define LOGTE(tag, fmt, ...) LOGE("[" tag "] " fmt, ##__VA_ARGS__)
