#pragma once

#include <SPIFFS.h>

namespace farmhub { namespace kernel {
class FileSystem {
public:
    virtual bool exists(const String& path) = 0;

    virtual File open(const String& path, const char* mode = FILE_READ) = 0;
};

class FileSystemDriver {
public:
    FileSystemDriver() {
    }

    const FileSystem& getFileSystem() const {
        return *fileSystem;
    }

private:
    const FileSystem* fileSystem { initFileSystem() };

    const FileSystem* initFileSystem() {
        Serial.print("Starting file system... ");
        if (!SPIFFS.begin()) {
            Serial.println("File system not initialized");
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
        return new RegularFileSystem();
    }

    class RegularFileSystem : public FileSystem {
    public:
        bool exists(const String& path) override {
            return SPIFFS.exists(path);
        }

        File open(const String& path, const char* mode = FILE_READ) override {
            return SPIFFS.open(path, mode);
        }
    };

    class UninitializedFileSystem : public FileSystem {
    public:
        bool exists(const String& path) override {
            return false;
        }

        File open(const String& path, const char* mode = FILE_READ) override {
            return File();
        }
    };
};

}}    // namespace farmhub::kernel
