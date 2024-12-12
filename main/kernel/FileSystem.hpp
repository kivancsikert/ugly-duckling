#pragma once

#include <dirent.h>
#include <expected>
#include <stdio.h>
#include <sys/stat.h>

#include <esp_spiffs.h>

namespace farmhub::kernel {

static constexpr const char* PARTITION = "data";

class FileSystem {
public:
    bool exists(const std::string& path) const {
        struct stat fileStat;
        return stat(resolve(path).c_str(), &fileStat) == 0;
    }

    FILE* open(const std::string& path, const char* mode) const {
        return fopen(resolve(path).c_str(), mode);
    }

    std::optional<std::string> readAll(const std::string& path) const {
        std::string resolvedPath = resolve(path);
        struct stat fileStat;
        if (stat(resolvedPath.c_str(), &fileStat) != 0) {
            return std::nullopt;
        }
        FILE* file = fopen(resolvedPath.c_str(), "r");
        if (file == nullptr) {
            return std::nullopt;
        }
        std::string result(fileStat.st_size, '\0');
        size_t bytesRead = fread(result.data(), 1, fileStat.st_size, file);
        fclose(file);
        if (bytesRead != fileStat.st_size) {
            return std::nullopt;
        }
        return result;
    }

    bool writeAll(const std::string& path, const std::string& content) const {
        FILE* file = fopen(resolve(path).c_str(), "w");
        if (file == nullptr) {
            return false;
        }
        size_t bytesWritten = fwrite(content.data(), 1, content.size(), file);
        fclose(file);
        return bytesWritten == content.size();
    }

    size_t size(const std::string& path) const {
        struct stat fileStat;
        if (stat(resolve(path).c_str(), &fileStat) != 0) {
            return 0;
        }
        return fileStat.st_size;
    }

    size_t read(const std::string& path, char* buffer, size_t size) const {
        FILE* file = open(resolve(path), "r");
        if (file == nullptr) {
            return 0;
        }

        size_t bytesRead = fread(buffer, 1, size, file);
        fclose(file);
        return bytesRead;
    }

    size_t write(const std::string& path, const char* buffer, size_t size) const {
        FILE* file = open(resolve(path), "w");
        if (file == nullptr) {
            return 0;
        }

        size_t bytesWritten = fwrite(buffer, 1, size, file);
        fclose(file);
        return bytesWritten;
    }

    bool readDir(const std::string& path, std::function<void(const std::string&, size_t)> callback) const {
        std::string resolvedPath = resolve(path);
        DIR* dir = opendir(resolvedPath.c_str());
        if (dir == nullptr) {
            LOGTE(Tag::FS, "Failed to open directory: %s", path.c_str());
            return false;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string fullPath = std::string(resolvedPath) + "/" + entry->d_name;
            callback(entry->d_name, size(fullPath));
        }

        closedir(dir);
        return true;
    }

    std::string resolve(const std::string& path) const {
        return mountPoint + path;
    }

    static bool format() {
        esp_err_t ret = esp_spiffs_format("data");
        if (ret == ESP_OK) {
            LOGTV(Tag::FS, "SPIFFS partition '%s' formatted successfully", PARTITION);
            return true;
        } else {
            LOGTE(Tag::FS, "Error formatting SPIFFS partition '%s': %s\n", PARTITION, esp_err_to_name(ret));
            return false;
        }
    }

    static FileSystem& get() {
        static FileSystem* instance = initializeFileSystem();
        return *instance;
    }

private:
    FileSystem(const std::string& mountPoint)
        : mountPoint(mountPoint) {
    }

    static FileSystem* initializeFileSystem() {
        const std::string mountPoint = "/" + std::string(PARTITION);
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
                LOGTI(Tag::FS, "SPIFFS partition '%s' mounted successfully", PARTITION);
                fs = new FileSystem(mountPoint);
                fs->readDir(mountPoint, [&](const std::string& name, size_t size) {
                    LOGTI(Tag::FS, " - %s (%u bytes)", name.c_str(), size);
                });
                break;
            }
            case ESP_FAIL:
                LOGTE(Tag::FS, "Failed to mount partition '%s'", PARTITION);
                break;
            case ESP_ERR_NOT_FOUND:
                LOGTE(Tag::FS, "Failed to find SPIFFS partition '%s'", PARTITION);
                break;
            default:
                LOGTE(Tag::FS, "Failed to initialize SPIFFS partition '%s' (%s)", PARTITION, esp_err_to_name(ret));
                break;
        }

        return fs;
    }

    const std::string mountPoint;
};

}    // namespace farmhub::kernel
