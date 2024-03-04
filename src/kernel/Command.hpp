#pragma once

#include <chrono>
#include <functional>

#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <SPIFFS.h>

#include <esp_sleep.h>

#include <kernel/FileSystem.hpp>
#include <kernel/Named.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub::kernel {

class Command : public Named {
public:
    virtual void handle(const JsonObject& request, JsonObject& response) = 0;

    virtual size_t getResponseSize() {
        return 1024;
    }

protected:
    Command(const String& name)
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
            Log.errorln("Failed to send ping response: %s", e.what());
        } catch (...) {
            Log.errorln("Failed to send ping response");
        }
        response["pong"] = millis();
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
        Serial.println("Restarting...");
        Serial.flush();
        ESP.restart();
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
        Log.infoln("Sleeping for %ld seconds in light sleep mode",
            duration.count());
        esp_deep_sleep_start();
    }
};

class FileCommand : public Command {
public:
    FileCommand(const String& name, FileSystem& fs)
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
        File root = fs.open("/", FILE_READ);
        JsonArray files = response.createNestedArray("files");
        while (true) {
            File entry = root.openNextFile();
            if (!entry) {
                break;
            }
            JsonObject file = files.createNestedObject();
            file["name"] = String(entry.name());
            file["size"] = entry.size();
            file["type"] = entry.isDirectory()
                ? "dir"
                : "file";
            entry.close();
        }
    }
};

class FileReadCommand : public FileCommand {
public:
    FileReadCommand(FileSystem& fs)
        : FileCommand("files/read", fs) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        String path = request["path"];
        if (!path.startsWith("/")) {
            path = "/" + path;
        }
        Log.infoln("Reading %s",
            path.c_str());
        response["path"] = path;
        File file = fs.open(path, FILE_READ);
        if (file) {
            response["size"] = file.size();
            response["contents"] = file.readString();
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
        String path = request["path"];
        if (!path.startsWith("/")) {
            path = "/" + path;
        }
        Log.infoln("Writing %s",
            path.c_str());
        String contents = request["contents"];
        response["path"] = path;
        File file = fs.open(path, FILE_WRITE);
        if (file) {
            auto written = file.print(contents);
            file.flush();
            response["written"] = written;
            file.close();
        } else {
            response["error"] = "File not found";
        }
    }
};

class FileRemoveCommand : public FileCommand {
public:
    FileRemoveCommand(FileSystem& fs)
        : FileCommand("files/remove", fs) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        String path = request["path"];
        if (!path.startsWith("/")) {
            path = "/" + path;
        }
        Log.infoln("Removing %s",
            path.c_str());
        response["path"] = path;
        if (fs.remove(path)) {
            response["removed"] = true;
        } else {
            response["error"] = "File not found";
        }
    }
};

class HttpUpdateCommand : public Command {
public:
    HttpUpdateCommand(const std::function<void(const String&)> prepareUpdate)
        : Command("update")
        , prepareUpdate(prepareUpdate) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        if (!request.containsKey("url")) {
            response["failure"] = "Command contains no URL";
            return;
        }
        String url = request["url"];
        if (url.length() == 0) {
            response["failure"] = "Command contains empty url";
            return;
        }
        prepareUpdate(url);
        response["success"] = true;
        Task::run("update", [](Task& task) {
            Log.infoln("Restarting in 5 seconds to apply update");
            delay(5000);
            ESP.restart();
        });
    }

private:
    const std::function<void(const String&)> prepareUpdate;
    const String currentVersion;
};

}    // namespace farmhub::kernel
