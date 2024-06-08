#pragma once

#include <atomic>
#include <functional>
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
#define FARMHUB_LOG_LEVEL_ALL 100

enum class Level {
    Silent = FARMHUB_LOG_LEVEL_SILENT,
    Fatal = FARMHUB_LOG_LEVEL_FATAL,
    Error = FARMHUB_LOG_LEVEL_ERROR,
    Warning = FARMHUB_LOG_LEVEL_WARNING,
    Info = FARMHUB_LOG_LEVEL_INFO,
    Debug = FARMHUB_LOG_LEVEL_DEBUG,
    Trace = FARMHUB_LOG_LEVEL_TRACE,
    All = FARMHUB_LOG_LEVEL_ALL
};

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

class LogConsumer {
public:
    virtual void consumeLog(Level level, const char* message) = 0;
};

class FarmhubLog : public LogConsumer {
public:
    inline void __attribute__((format(printf, 2, 3))) fatal(const char* format, ...) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_FATAL
        va_list args;
        va_start(args, format);
        logImpl(Level::Fatal, format, args);
        va_end(args);
#endif
    }

    inline void __attribute__((format(printf, 2, 3))) error(const char* format, ...) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_ERROR
        va_list args;
        va_start(args, format);
        logImpl(Level::Error, format, args);
        va_end(args);
#endif
    }

    inline void __attribute__((format(printf, 2, 3))) warn(const char* format, ...) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_WARNING
        va_list args;
        va_start(args, format);
        logImpl(Level::Warning, format, args);
        va_end(args);
#endif
    }

    inline void __attribute__((format(printf, 2, 3))) info(const char* format, ...) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_INFO
        va_list args;
        va_start(args, format);
        logImpl(Level::Info, format, args);
        va_end(args);
#endif
    }

    inline void __attribute__((format(printf, 2, 3))) debug(const char* format, ...) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_DEBUG
        va_list args;
        va_start(args, format);
        logImpl(Level::Debug, format, args);
        va_end(args);
#endif
    }

    inline void __attribute__((format(printf, 2, 3))) trace(const char* format, ...) {
#if FARMHUB_LOG_LEVEL >= FARMHUB_LOG_LEVEL_TRACE
        va_list args;
        va_start(args, format);
        logImpl(Level::Trace, format, args);
        va_end(args);
#endif
    }

    void __attribute__((format(printf, 3, 4))) log(Level level, const char* format, ...) {
        va_list args;
        va_start(args, format);
        logImpl(level, format, args);
        va_end(args);
    }

    void log(Level level, const __FlashStringHelper* format, ...) {
        int len = strnlen_P(reinterpret_cast<const char*>(format), bufferSize - 1);
        char formatInSram[len + 1];
        strncpy_P(formatInSram, reinterpret_cast<const char*>(format), len);
        formatInSram[len] = '\0';
        va_list args;
        va_start(args, format);
        logImpl(level, formatInSram, args);
        va_end(args);
    }

    void setConsumer(LogConsumer* consumer) {
        this->consumer = consumer;
    }

    /**
     * @brief Default implementation of LogConsumer.
     */
    void consumeLog(Level level, const char* message) override {
        Serial.println(message);
    }

private:
    void logImpl(Level level, const char* format, va_list args) {
        if (static_cast<int>(level) < FARMHUB_LOG_LEVEL) {
            return;
        }

        int size = vsnprintf(nullptr, 0, format, args);
        if (size <= 0) {
            return;
        }

        if (size < bufferSize) {
            Lock lock(bufferMutex);
            logWithBuffer(buffer, size + 1, level, format, args);
        } else {
            char* localBuffer = new char[size + 1];
            logWithBuffer(localBuffer, size + 1, level, format, args);
            delete localBuffer;
        }
    }

    void logWithBuffer(char* buffer, size_t size, Level level, const char* format, va_list args) {
        vsnprintf(buffer, size, format, args);
        consumer.load()->consumeLog(level, buffer);
    }

    constexpr static int bufferSize = 128;
    // TODO Maybe use a thread_local here?
    Mutex bufferMutex;
    char buffer[bufferSize];
    std::atomic<LogConsumer*> consumer { this };
};

extern FarmhubLog Log;
FarmhubLog Log = FarmhubLog();

}    // namespace farmhub::kernel
