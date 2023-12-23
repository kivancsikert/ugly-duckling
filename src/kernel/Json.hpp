#pragma once

#include <ArduinoJson.h>
#include <SPIFFS.h>

namespace farmhub { namespace kernel {

size_t constexpr _docSizeFor(size_t size) {
    return size * 2 + 1024;
}

size_t inline docSizeFor(const String& json) {
    return _docSizeFor(json.length());
}

size_t inline docSizeFor(const char* json) {
    return _docSizeFor(strlen(json));
}

size_t inline docSizeFor(const File file) {
    return _docSizeFor(file.size());
}

}}
