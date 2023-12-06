#pragma once

#include <SPIFFS.h>

namespace farmhub { namespace kernel {

class FileSystem;
static FileSystem* initializeFileSystem();

class FileSystem {
public:
    virtual bool exists(const String& path) = 0;

    virtual File open(const String& path, const char* mode = FILE_READ) = 0;

    virtual bool remove(const String& path) = 0;

    static FileSystem& get() {
        static FileSystem* instance = initializeFileSystem();
        return *instance;
    }

private:
};

class SpiffsFileSystem : public FileSystem {
public:
    SpiffsFileSystem() {
    }

    bool exists(const String& path) override {
        return SPIFFS.exists(path);
    }

    File open(const String& path, const char* mode = FILE_READ) override {
        return SPIFFS.open(path, mode);
    }

    bool remove(const String& path) override {
        return SPIFFS.remove(path);
    }
};

class UninitializedFileSystem : public FileSystem {
public:
    UninitializedFileSystem() {
    }

    bool exists(const String& path) override {
        return false;
    }

    File open(const String& path, const char* mode = FILE_READ) override {
        return File();
    }

    bool remove(const String& path) override {
        return false;
    }
};

static FileSystem* initializeFileSystem() {
    Serial.print("Starting file system... ");
    if (!SPIFFS.begin()) {
        Serial.println("file system not initialized");
        return new UninitializedFileSystem();
    }
    Serial.println("contents:");
    File root = SPIFFS.open("/", FILE_READ);
    while (true) {
        File file = root.openNextFile();
        if (!file) {
            break;
        }
        Serial.printf(" - %s (%d bytes)\n", file.name(), file.size());
        file.close();
    }
    return new SpiffsFileSystem();
}

}}    // namespace farmhub::kernel
