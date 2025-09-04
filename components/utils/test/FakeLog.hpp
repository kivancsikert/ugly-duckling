#pragma once

#define LOGGING_TAG(varName, name) \
    static constexpr const char* varName = name;

LOGGING_TAG(TEST, "test")

#define LOG(fmt, ...)                         \
    do {                                      \
        std::printf(fmt "\n", ##__VA_ARGS__); \
    } while (0)

#define LOGV(fmt, ...) LOG("V " fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LOG("D " fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG("I " fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG("W " fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG("E " fmt, ##__VA_ARGS__)

#define LOGTV(tag, fmt, ...) LOGV("[%s] " fmt, tag, ##__VA_ARGS__)
#define LOGTD(tag, fmt, ...) LOGD("[%s] " fmt, tag, ##__VA_ARGS__)
#define LOGTI(tag, fmt, ...) LOGI("[%s] " fmt, tag, ##__VA_ARGS__)
#define LOGTW(tag, fmt, ...) LOGW("[%s] " fmt, tag, ##__VA_ARGS__)
#define LOGTE(tag, fmt, ...) LOGE("[%s] " fmt, tag, ##__VA_ARGS__)
