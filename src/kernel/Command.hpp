#pragma once

#include <chrono>

#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <HTTPUpdate.h>
#include <SPIFFS.h>

#include <esp_sleep.h>

#include <kernel/FileSystem.hpp>
#include <kernel/Telemetry.hpp>

using namespace std::chrono;

namespace farmhub { namespace kernel {

class Command {
public:
    virtual void handle(const JsonObject& request, JsonObject& response) = 0;

    const String name;

protected:
    Command(const String& name)
        : name(name) {
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
    PingCommand(TelemetryPublisher& telemetryPublisher)
        : Command("ping")
        , telemetryPublisher(telemetryPublisher) {
    }

    void handle(const JsonObject& request, JsonObject& response) override {
        telemetryPublisher.publishTelemetry();
        response["pong"] = millis();
    }

private:
    TelemetryPublisher& telemetryPublisher;
};

class RestartCommand : public Command {
public:
    RestartCommand()
        : Command("restart") {
    }
    void handle(const JsonObject& request, JsonObject& response) override {
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
    HttpUpdateCommand(const String& currentVersion)
        : Command("update")
        , currentVersion(currentVersion) {
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
        httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        response["failure"] = update(url, currentVersion);
    }

private:
    static String update(const String& url, const String& currentVersion) {
        Log.infoln("Updating from version %s via URL %s",
            currentVersion.c_str(), url.c_str());
        WiFiClientSecure client;
        // Allow insecure connections for testing
        client.setInsecure();
        HTTPUpdateResult result = httpUpdate.update(client, url, currentVersion);
        switch (result) {
            case HTTP_UPDATE_FAILED:
                return httpUpdate.getLastErrorString() + " (" + String(httpUpdate.getLastError()) + ")";
            case HTTP_UPDATE_NO_UPDATES:
                return "No updates available";
            case HTTP_UPDATE_OK:
                return "Update OK";
            default:
                return "Unknown response";
        }
    }

    const String currentVersion;
};

}}    // namespace farmhub::kernel
