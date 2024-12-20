#pragma once

#include <chrono>
#include <functional>

#include <ArduinoJson.h>

#include <esp_sleep.h>
#include <esp_system.h>

#include <kernel/BootClock.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/Named.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub::kernel {

class Command : public Named {
public:
    virtual void handle(const JsonObject& request, JsonObject& response) = 0;

protected:
    Command(const std::string& name)
        : Named(name) {
    }
};

class EchoCommand : public Command {
public:
    EchoCommand()
        : Command("echo") {
    }
    void handle(const JsonObject& request, JsonObject& response) override {
        response["original"] = request;
    }
};

class PingCommand : public Command {
public:
    PingCommand(const std::function<void()> pingResponse)
        : Command("ping")
        , pingResponse(pingResponse) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        try {
            pingResponse();
        } catch (const std::exception& e) {
            LOGE("Failed to send ping response: %s", e.what());
        } catch (...) {
            LOGE("Failed to send ping response");
        }
        response["pong"] = duration_cast<milliseconds>(boot_clock::now().time_since_epoch()).count();
    }

private:
    const std::function<void()> pingResponse;
};

class RestartCommand : public Command {
public:
    RestartCommand()
        : Command("restart") {
    }
    void handle(const JsonObject& request, JsonObject& response) override {
        printf("Restarting...\n");
        fflush(stdout);
        fsync(fileno(stdout));
        esp_restart();
    }
};

class SleepCommand : public Command {
public:
    SleepCommand()
        : Command("sleep") {
    }
    void handle(const JsonObject& request, JsonObject& response) override {
        seconds duration = seconds(request["duration"].as<long>());
        esp_sleep_enable_timer_wakeup(((microseconds) duration).count());
        LOGI("Sleeping for %lld seconds in light sleep mode",
            duration.count());
        esp_deep_sleep_start();
    }
};

class FileCommand : public Command {
public:
    FileCommand(const std::string& name, FileSystem& fs)
        : Command(name)
        , fs(fs) {
    }

protected:
    FileSystem& fs;
};

class FileListCommand : public FileCommand {
public:
    FileListCommand(FileSystem& fs)
        : FileCommand("files/list", fs) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        JsonArray files = response["files"].to<JsonArray>();
        fs.readDir("/", [&](const std::string& name, off_t size) {
            JsonObject file = files.add<JsonObject>();
            file["name"] = name;
            file["size"] = size;
        });
    }
};

class FileReadCommand : public FileCommand {
public:
    FileReadCommand(FileSystem& fs)
        : FileCommand("files/read", fs) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        std::string path = request["path"];
        if (!path.starts_with("/")) {
            path = "/" + path;
        }
        LOGI("Reading %s",
            path.c_str());
        response["path"] = path;
        if (fs.exists(path)) {
            response["size"] = fs.size(path);
            auto contents = fs.readAll(path);
            if (contents.has_value()) {
                response["contents"] = contents.value();
            }
        } else {
            response["error"] = "File not found";
        }
    }
};

class FileWriteCommand : public FileCommand {
public:
    FileWriteCommand(FileSystem& fs)
        : FileCommand("files/write", fs) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        std::string path = request["path"];
        if (!path.starts_with("/")) {
            path = "/" + path;
        }
        LOGI("Writing %s",
            path.c_str());
        std::string contents = request["contents"];
        response["path"] = path;
        size_t written = fs.writeAll(path, contents);
        response["written"] = written;
    }
};

class FileRemoveCommand : public FileCommand {
public:
    FileRemoveCommand(FileSystem& fs)
        : FileCommand("files/remove", fs) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        std::string path = request["path"];
        if (!path.starts_with("/")) {
            path = "/" + path;
        }
        LOGI("Removing %s",
            path.c_str());
        response["path"] = path;
        int err = fs.remove(path);
        if (err == 0) {
            response["removed"] = true;
        } else {
            response["error"] = "File not found: " + std::to_string(err);
        }
    }
};

class HttpUpdateCommand : public Command {
public:
    HttpUpdateCommand(const std::function<void(const std::string&)> prepareUpdate)
        : Command("update")
        , prepareUpdate(prepareUpdate) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        if (!request["url"].is<std::string>()) {
            response["failure"] = "Command contains no URL";
            return;
        }
        std::string url = request["url"];
        if (url.length() == 0) {
            response["failure"] = "Command contains empty url";
            return;
        }
        prepareUpdate(url);
        response["success"] = true;
        Task::run("update", 3072, [](Task& task) {
            LOGI("Restarting in 5 seconds to apply update");
            Task::delay(5s);
            esp_restart();
        });
    }

private:
    const std::function<void(const std::string&)> prepareUpdate;
    const std::string currentVersion;
};

}    // namespace farmhub::kernel
