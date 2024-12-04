#pragma once

#include <Arduino.h>

#include <kernel/Log.hpp>

namespace farmhub::kernel {

struct LogRecord {
    Level level;
    String message;
};

#define FARMHUB_LOG_COLOR_BLACK "30"
#define FARMHUB_LOG_COLOR_RED "31"
#define FARMHUB_LOG_COLOR_GREEN "32"
#define FARMHUB_LOG_COLOR_BROWN "33"
#define FARMHUB_LOG_COLOR_BLUE "34"
#define FARMHUB_LOG_COLOR_PURPLE "35"
#define FARMHUB_LOG_COLOR_CYAN "36"
#define FARMHUB_LOG_COLOR(COLOR) "\033[0;" COLOR "m"
#define FARMHUB_LOG_BOLD(COLOR) "\033[1;" COLOR "m"
#define FARMHUB_LOG_RESET_COLOR "\033[0m"

class ConsoleProvider;

static ConsoleProvider* consoleProvider;

class ConsoleProvider {
public:
    ConsoleProvider(Queue<LogRecord>& logRecords, Level recordedLevel)
        : logRecords(logRecords)
        , recordedLevel(recordedLevel) {
        consoleProvider = this;
        originalVprintf = esp_log_set_vprintf(ConsoleProvider::processLogFunc);
    }

private:
    static int processLogFunc(const char* format, va_list args) {
        return consoleProvider->processLog(format, args);
    }

    int processLog(const char* format, va_list args) {
        Level level = getLevel(format[0]);
        if (level <= recordedLevel) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            vsnprintf(buffer, BUFFER_SIZE, format, args);
            logRecords.offer(level, buffer);
        }

        int count = 0;
#ifdef FARMHUB_DEBUG
        // Erase the current line
        count += printf("\033[1G\033[0K");
        switch (level) {
            case Level::Error:
                count += printf(FARMHUB_LOG_COLOR(FARMHUB_LOG_COLOR_RED));
                break;
            case Level::Warning:
                count += printf(FARMHUB_LOG_COLOR(FARMHUB_LOG_COLOR_BROWN));
                break;
            case Level::Info:
                count += printf(FARMHUB_LOG_COLOR(FARMHUB_LOG_COLOR_GREEN));
                break;
            default:
                break;
        }
#endif

        int originalCount = originalVprintf(format, args);
        if (originalCount < 0) {
            return originalCount;
        }
        count += originalCount;

#ifdef FARMHUB_DEBUG
        switch (level) {
            case Level::Error:
            case Level::Warning:
            case Level::Info:
                count += printf(FARMHUB_LOG_RESET_COLOR);
                break;
            default:
                break;
        }
#endif
        return count;
    }

    static inline Level getLevel(char c) {
        switch (c) {
            case 'E':
                return Level::Error;
            case 'W':
                return Level::Warning;
            case 'I':
                return Level::Info;
            case 'D':
                return Level::Debug;
            case 'V':
                return Level::Verbose;
            default:
                return Level::Info;
        }
    }

    vprintf_like_t originalVprintf;
    Queue<LogRecord>& logRecords;
    const Level recordedLevel;
    std::mutex bufferMutex;
    static constexpr size_t BUFFER_SIZE = 128;
    char buffer[BUFFER_SIZE];
};

}    // namespace farmhub::kernel
