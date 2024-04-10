#pragma once

#include <algorithm>
#include <functional>
#include <list>
#include <memory>

#include <ArduinoJson.h>

#include <kernel/Concurrent.hpp>

namespace farmhub::kernel {

#define FARMHUB_LOG_LEVEL_SILENT 0
#define FARMHUB_LOG_LEVEL_FATAL 1
#define FARMHUB_LOG_LEVEL_ERROR 2
#define FARMHUB_LOG_LEVEL_WARNING 3
#define FARMHUB_LOG_LEVEL_INFO 4
#define FARMHUB_LOG_LEVEL_DEBUG 5
#define FARMHUB_LOG_LEVEL_TRACE 6

enum class Level {
    Silent = FARMHUB_LOG_LEVEL_SILENT,
    Fatal = FARMHUB_LOG_LEVEL_FATAL,
    Error = FARMHUB_LOG_LEVEL_ERROR,
    Warning = FARMHUB_LOG_LEVEL_WARNING,
    Info = FARMHUB_LOG_LEVEL_INFO,
    Debug = FARMHUB_LOG_LEVEL_DEBUG,
    Trace = FARMHUB_LOG_LEVEL_TRACE
};

using LogConsumer = std::function<void(Level, const char*)>;

#ifndef FARMHUB_LOG_LEVEL
#ifdef FARMHUB_DEBUG
#define FARMHUB_LOG_LEVEL FARMHUB_LOG_LEVEL_TRACE
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

class FarmhubLog {
public:
    template <class T, typename... Args>
    inline void fatal(T format, Args... args) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_FATAL
        log(Level::Fatal, format, args...);
#endif
    }

    template <class T, typename... Args>
    inline void error(T format, Args... args) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_ERROR
        log(Level::Error, format, args...);
#endif
    }

    template <class T, typename... Args>
    inline void warn(T format, Args... args) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_WARNING
        log(Level::Warning, format, args...);
#endif
    }

    template <class T, typename... Args>
    inline void info(T format, Args... args) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_INFO
        log(Level::Info, format, args...);
#endif
    }

    template <class T, typename... Args>
    inline void debug(T format, Args... args) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_DEBUG
        log(Level::Debug, format, args...);
#endif
    }

    template <class T, typename... Args>
    inline void trace(T format, Args... args) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_TRACE
        log(Level::Trace, format, args...);
#endif
    }

    template <typename... Args>
    void log(Level level, const __FlashStringHelper* format, Args... args) {
        int len = strnlen_P(reinterpret_cast<const char*>(format), bufferSize - 1);
        char formatInSram[len + 1];
        strncpy_P(formatInSram, reinterpret_cast<const char*>(format), len);
        formatInSram[len] = '\0';
        log(level, formatInSram, args...);
    }

    template <typename... Args>
    void log(Level level, const char* format, Args... args) {
        int size = snprintf(nullptr, 0, format, args...);
        if (size <= 0) {
            return;
        }

        Lock lock(mutex);
        if (size < bufferSize) {
            logWithBuffer(buffer, size + 1, level, format, args...);
        } else {
            char* localBuffer = new char[size + 1];
            logWithBuffer(localBuffer, size + 1, level, format, args...);
            delete localBuffer;
        }
    }

    void updateConsumers(std::function<void(std::list<LogConsumer>&)> consumerUpdater) {
        Lock lock(mutex);
        consumerUpdater(consumers);
    }

private:
    template <typename... Args>
    void logWithBuffer(char* buffer, size_t size, Level level, const char* format, Args... args) {
        snprintf(buffer, size, format, args...);
        for (auto& consumer : consumers) {
            consumer(level, buffer);
        }
    }

    Mutex mutex;
    constexpr static int bufferSize = 128;
    char buffer[bufferSize];
    std::list<LogConsumer> consumers;
};

extern FarmhubLog Log;
FarmhubLog Log = FarmhubLog();

}    // namespace farmhub::kernel
