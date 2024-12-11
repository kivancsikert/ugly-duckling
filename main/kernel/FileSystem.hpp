#pragma once

#include <dirent.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <sys/stat.h>

#include <esp_spiffs.h>

#include <Arduino.h>

namespace farmhub::kernel {

static constexpr const char* PARTITION = "data";

class FileSystem {
public:
    bool exists(const String& path) const {
        struct stat fileStat;
        const char* resolvedPath = resolve(path).c_str();
        return stat(resolvedPath, &fileStat) == 0;
    }

    FILE* open(const String& path, const char* mode) const {
        return fopen(resolve(path).c_str(), mode);
    }

    std::ifstream openRead(const String& path) const {
        return std::ifstream(resolve(path).c_str());
    }

    std::ofstream openWrite(const String& path) const {
        return std::ofstream(resolve(path).c_str());
    }

    size_t size(const String& path) const {
        struct stat fileStat;
        if (stat(resolve(path).c_str(), &fileStat) != 0) {
            return 0;
        }
        return fileStat.st_size;
    }

    size_t read(const String& path, char* buffer, size_t size) const {
        FILE* file = open(resolve(path), "r");
        if (file == nullptr) {
            return 0;
        }

        size_t bytesRead = fread(buffer, 1, size, file);
        fclose(file);
        return bytesRead;
    }

    size_t write(const String& path, const char* buffer, size_t size) const {
        FILE* file = open(resolve(path), "w");
        if (file == nullptr) {
            return 0;
        }

        size_t bytesWritten = fwrite(buffer, 1, size, file);
        fclose(file);
        return bytesWritten;
    }

    bool readDir(const String& path, std::function<void(const String&, size_t)> callback) const {
        String resolvedPath = resolve(path);
        DIR* dir = opendir(resolvedPath.c_str());
        if (dir == nullptr) {
            LOGTE("fs", "Failed to open directory: %s", path.c_str());
            return false;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            String fullPath = String(resolvedPath) + "/" + entry->d_name;
            callback(entry->d_name, size(fullPath));
        }

        closedir(dir);
        return true;
    }

    String resolve(const String& path) const {
        return mountPoint + path;
    }

    static bool format() {
        esp_err_t ret = esp_spiffs_format("data");
        if (ret == ESP_OK) {
            LOGTV("fs", "SPIFFS partition '%s' formatted successfully", PARTITION);
            return true;
        } else {
            LOGTE("fs", "Error formatting SPIFFS partition '%s': %s\n", PARTITION, esp_err_to_name(ret));
            return false;
        }
    }

    static FileSystem& get() {
        static FileSystem* instance = initializeFileSystem();
        return *instance;
    }

private:
    FileSystem(const String& mountPoint)
        : mountPoint(mountPoint) {
    }

    static FileSystem* initializeFileSystem() {
        const String mountPoint = "/" + String(PARTITION);
        FileSystem* fs = nullptr;

        esp_vfs_spiffs_conf_t conf = {
            .base_path = mountPoint.c_str(),
            .partition_label = PARTITION,
            .max_files = 5,
            .format_if_mount_failed = false
        };
        esp_err_t ret = esp_vfs_spiffs_register(&conf);

        switch (ret) {
            case ESP_OK: {
                LOGTI("fs", "SPIFFS partition '%s' mounted successfully", PARTITION);
                fs = new FileSystem(mountPoint);
                fs->readDir(mountPoint, [&](const String& name, size_t size) {
                    LOGTI("fs", " - %s (%u bytes)", name.c_str(), size);
                });
                break;
            }
            case ESP_FAIL:
                LOGTE("fs", "Failed to mount partition '%s'", PARTITION);
                break;
            case ESP_ERR_NOT_FOUND:
                LOGTE("fs", "Failed to find SPIFFS partition '%s'", PARTITION);
                break;
            default:
                LOGTE("fs", "Failed to initialize SPIFFS partition '%s' (%s)", PARTITION, esp_err_to_name(ret));
                break;
        }

        return fs;
    }

    const String mountPoint;
};

}    // namespace farmhub::kernel
