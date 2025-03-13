#pragma once

#include <LogJson.hpp>
#include <Task.hpp>
#include <mqtt/MqttRoot.hpp>

namespace farmhub::kernel::mqtt {

class MqttLog {
public:
    static void init(Level publishLevel, std::shared_ptr<Queue<LogRecord>> logRecords, std::shared_ptr<MqttRoot> mqttRoot) {
        Task::loop("mqtt:log", 3072, [publishLevel, logRecords, mqttRoot](Task& task) {
            logRecords->take([&](const LogRecord& record) {
                if (record.level > publishLevel) {
                    return;
                }
                auto length = record.message.length();
                // Remove the level prefix
                auto messageStart = 2;
                // Remove trailing newline
                auto messageEnd = record.message[length - 1] == '\n'
                    ? length - 1
                    : length;
                std::string message = record.message.substr(messageStart, messageEnd - messageStart);

                mqttRoot->publish(
                    "log", [level = record.level, message](JsonObject& json) {
                        json["level"] = level;
                        json["message"] = message;
                    },
                    Retention::NoRetain, QoS::ExactlyOnce, 2s, LogPublish::Silent);
            });
        });
    }
};

}    // namespace farmhub::kernel::mqtt
