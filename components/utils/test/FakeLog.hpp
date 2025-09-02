#pragma once

#define LOG(fmt, ...) \
    do { \
        std::printf(fmt "\n", ##__VA_ARGS__); \
    } while (0)

#define LOGV(fmt, ...) LOG("[V] " fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LOG("[D] " fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG("[I] " fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG("[W] " fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG("[E] " fmt, ##__VA_ARGS__)
