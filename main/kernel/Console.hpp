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
        String message = consoleProvider->renderMessage(format, args);
        return consoleProvider->processLog(message);
    }

    int processLog(const String& message) {
        if (message.isEmpty()) {
            return 0;
        }

        String assembledMessage;
        {
            std::lock_guard<std::mutex> lock(partialMessageMutex);
            if (message.charAt(message.length() - 1) != '\n') {
                partialMessage += message;
                return 0;
            } else if (!partialMessage.isEmpty()) {
                assembledMessage = partialMessage + message;
                partialMessage.clear();
            }
        }
        if (assembledMessage.isEmpty()) {
            return processLogLine(message);
        } else {
            return processLogLine(assembledMessage);
        }
    }

    int processLogLine(const String& message) {
        Level level = getLevel(message);
        if (level <= recordedLevel) {
            logRecords.offer(level, message);
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
            case Level::Debug:
                count += printf(FARMHUB_LOG_COLOR(FARMHUB_LOG_COLOR_CYAN));
                break;
            case Level::Verbose:
                count += printf(FARMHUB_LOG_COLOR(FARMHUB_LOG_COLOR_BLUE));
                break;
            default:
                break;
        }
#endif

        count += printf("%s", message.c_str());

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

    String renderMessage(const char* format, va_list args) {
        int length;
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            length = vsnprintf(buffer, BUFFER_SIZE, format, args);
            if (length < 0) {
                return "<Encoding error>";
            } else if (length < BUFFER_SIZE) {
                return String(buffer, length);
            }
        }

        // The buffer was too small, try again with a heap-allocated buffer instead, but still limit length
        length = std::min(length, 2048);
        char* heapBuffer = new char[length + 1];
        vsnprintf(heapBuffer, length + 1, format, args);
        String result(heapBuffer, length);
        delete[] heapBuffer;
        return result;
    }

    static Level getLevel(const String& message) {
        // Anything that doesn't look like 'X ...' is a debug message
        if (message.length() < 2 || message.charAt(1) != ' ') {
            return Level::Debug;
        }
        switch (message.charAt(0)) {
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
                // Anything with an unknown level is a debug message
                return Level::Debug;
        }
    }

    vprintf_like_t originalVprintf;
    Queue<LogRecord>& logRecords;
    const Level recordedLevel;
    std::mutex bufferMutex;
    static constexpr size_t BUFFER_SIZE = 128;
    char buffer[BUFFER_SIZE];

    std::mutex partialMessageMutex;
    String partialMessage;
};

}    // namespace farmhub::kernel
