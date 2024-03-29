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
#include <string>

typedef std::string String;

#define LOG_DEBUG(...) printf(__VA_ARGS__)
#define LOG_TRACE(...) printf(__VA_ARGS__)
#define LOG_INFO(...) printf(__VA_ARGS__)
#define LOG_WARN(...) printf(__VA_ARGS__)
#define LOG_ERROR(...) printf(__VA_ARGS__)

#define LOG_IMMEDIATE(...) printf(__VA_ARGS__)
#endif
