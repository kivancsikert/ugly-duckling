#pragma once

#include <SPIFFS.h>

#include <kernel/Log.hpp>

namespace farmhub::kernel {

class FileSystem;
static FileSystem* initializeFileSystem();

class FileSystem {
public:
    virtual bool exists(const String& path) const = 0;

    virtual File open(const String& path, const char* mode = FILE_READ) const = 0;

    virtual bool remove(const String& path) const = 0;

    virtual bool reset() const = 0;

    static FileSystem& get() {
        static FileSystem* instance = initializeFileSystem();
        return *instance;
    }
};

class SpiffsFileSystem : public FileSystem {
public:
    SpiffsFileSystem() {
    }

    bool exists(const String& path) const override {
        return SPIFFS.exists(path);
    }

    File open(const String& path, const char* mode = FILE_READ) const override {
        return SPIFFS.open(path, mode);
    }

    bool remove(const String& path) const override {
        return SPIFFS.remove(path);
    }

    bool reset() const override {
        return SPIFFS.format();
    }
};

class UninitializedFileSystem : public FileSystem {
public:
    UninitializedFileSystem() {
    }

    bool exists(const String& path) const override {
        return false;
    }

    File open(const String& path, const char* mode = FILE_READ) const override {
        return File();
    }

    bool remove(const String& path) const override {
        return false;
    }

    bool reset() const override {
        return false;
    }
};

static FileSystem* initializeFileSystem() {
    if (!SPIFFS.begin()) {
        LOGI("File system not initialized");
        return new UninitializedFileSystem();
    }
    LOGI("File system contents:");
    File root = SPIFFS.open("/", FILE_READ);
    while (true) {
        File file = root.openNextFile();
        if (!file) {
            break;
        }
        LOGI(" - %s (%d bytes)",
            file.path(), file.size());
        file.close();
    }
    return new SpiffsFileSystem();
}

}    // namespace farmhub::kernel
