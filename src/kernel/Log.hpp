#pragma once

#ifdef ARDUINO
#include <ArduinoLog.h>

#define LOG_DEBUG(...) Log.verboseln(__VA_ARGS__)
#define LOG_TRACE(...) Log.traceln(__VA_ARGS__)
#define LOG_INFO(...) Log.infoln(__VA_ARGS__)
#define LOG_WARN(...) Log.warningln(__VA_ARGS__)
#define LOG_ERROR(...) Log.errorln(__VA_ARGS__)

#define LOG_IMMEDIATE(...) Serial.printf(__VA_ARGS__)
#else
#include <cstdarg>
#include <string>

typedef std::string String;

std::string __replaceAllOccurrences(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();    // Handles case where 'to' is a substring of 'from'
    }
    return str;
}

void __log(FILE* stream, const std::string& prefix, const std::string& _fmt, ...) {
    std::string fmt = prefix + __replaceAllOccurrences(_fmt, "%F", "%f") + std::string("\n");
    va_list args;
    va_start(args, fmt);
    vfprintf(stream, fmt.c_str(), args);
    va_end(args);
}

#define LOG_DEBUG(...) __log(stdout, "[VERBOSE] ", __VA_ARGS__)
#define LOG_TRACE(...) __log(stdout, "  [TRACE] ", __VA_ARGS__)
#define LOG_INFO(...) __log(stdout, "   [INFO] ", __VA_ARGS__)
#define LOG_WARN(...) __log(stdout, "[WARNING] ", __VA_ARGS__)
#define LOG_ERROR(...) __log(stdout, "  [ERROR] ", __VA_ARGS__)

#define LOG_IMMEDIATE(...) __log(stderr, "", __VA_ARGS__)
#endif
