#pragma once

#include <chrono>
#include <list>
#include <memory>

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <MQTTPubSubClient.h>

#include <kernel/Command.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/State.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/WiFiDriver.hpp>

using namespace farmhub::kernel;
using std::make_shared;
using std::shared_ptr;

namespace farmhub::kernel::drivers {

class MqttDriver {
public:
    enum class Retention {
        NoRetain,
        Retain
    };

    enum class QoS {
        AtMostOnce = 0,
        AtLeastOnce = 1,
        ExactlyOnce = 2
    };

    enum class PublishStatus {
        TimeOut = 0,
        Success = 1,
        Failed = 2,
        Pending = 3,
        QueueFull = 4
    };

    typedef std::function<void(const JsonObject&, JsonObject&)> CommandHandler;

    typedef std::function<void(const String&, const JsonObject&)> SubscriptionHandler;

    class MqttRoot {
    public:
        MqttRoot(MqttDriver& mqtt, const String& rootTopic)
            : mqtt(mqtt)
            , rootTopic(rootTopic) {
        }

        shared_ptr<MqttRoot> forSuffix(const String& suffix) {
            return make_shared<MqttRoot>(mqtt, rootTopic + "/" + suffix);
        }

        PublishStatus publish(const String& suffix, const JsonDocument& json, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = ticks::zero()) {
            return mqtt.publish(fullTopic(suffix), json, retain, qos, timeout);
        }

        PublishStatus publish(const String& suffix, std::function<void(JsonObject&)> populate, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = ticks::zero(), int size = MQTT_BUFFER_SIZE) {
            DynamicJsonDocument doc(size);
            JsonObject root = doc.to<JsonObject>();
            populate(root);
            return publish(suffix, doc, retain, qos, timeout);
        }

        PublishStatus clear(const String& suffix, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = ticks::zero()) {
            return mqtt.clear(fullTopic(suffix), retain, qos, timeout);
        }

        bool subscribe(const String& suffix, SubscriptionHandler handler) {
            return subscribe(suffix, QoS::ExactlyOnce, handler);
        }

        bool registerCommand(const String& name, CommandHandler handler) {
            return registerCommand(name, 1024, handler);
        }

        bool registerCommand(const String& name, size_t responseSize, CommandHandler handler) {
            String suffix = "commands/" + name;
            return subscribe(suffix, QoS::ExactlyOnce, [this, name, suffix, responseSize, handler](const String&, const JsonObject& request) {
                // TODO Do exponential backoff when clear cannot be finished
                // Clear topic and wait for it to be cleared
                auto clearStatus = mqtt.clear(fullTopic(suffix), Retention::Retain, QoS::ExactlyOnce, std::chrono::seconds { 5 });
                if (clearStatus != PublishStatus::Success) {
                    Log.errorln("MQTT: Failed to clear retained command topic '%s', status: %d", suffix.c_str(), clearStatus);
                }

                DynamicJsonDocument responseDoc(responseSize);
                auto response = responseDoc.to<JsonObject>();
                handler(request, response);
                if (response.size() > 0) {
                    publish("responses/" + name, responseDoc, Retention::NoRetain, QoS::ExactlyOnce);
                }
            });
        }

        void registerCommand(Command& command) {
            registerCommand(command.name, command.getResponseSize(), [&](const JsonObject& request, JsonObject& response) {
                command.handle(request, response);
            });
        }

        /**
         * @brief Subscribes to the given topic under the topic prefix.
         *
         * Note that subscription does not support wildcards.
         */
        bool subscribe(const String& suffix, QoS qos, SubscriptionHandler handler) {
            return mqtt.subscribe(fullTopic(suffix), qos, handler);
        }

    private:
        String fullTopic(const String& suffix) const {
            return rootTopic + "/" + suffix;
        }

        MqttDriver& mqtt;
        const String rootTopic;
    };

private:
    struct Message {
        const String topic;
        const String payload;
        const Retention retain;
        const QoS qos;
        const TaskHandle_t waitingTask;

        static const uint32_t PUBLISH_SUCCESS = 1;
        static const uint32_t PUBLISH_FAILED = 2;

        Message(const String& topic, const String& payload, Retention retention, QoS qos, TaskHandle_t waitingTask)
            : topic(topic)
            , payload(payload)
            , retain(retention)
            , qos(qos)
            , waitingTask(waitingTask) {
        }

        Message(const String& topic, const JsonDocument& payload, Retention retention, QoS qos, TaskHandle_t waitingTask)
            : topic(topic)
            , payload(serializeJsonToString(payload))
            , retain(retention)
            , qos(qos)
            , waitingTask(waitingTask) {
        }

    private:
        static String serializeJsonToString(const JsonDocument& jsonPayload) {
            String payload;
            serializeJson(jsonPayload, payload);
            return payload;
        }
    };

    struct Subscription {
        const String topic;
        const QoS qos;
        const SubscriptionHandler handle;

        Subscription(const String& topic, QoS qos, SubscriptionHandler handle)
            : topic(topic)
            , qos(qos)
            , handle(handle) {
        }

        Subscription(const Subscription& other)
            : topic(other.topic)
            , qos(other.qos)
            , handle(other.handle) {
        }
    };

public:
    class Config : public ConfigurationSection {
    public:
        Property<String> host { this, "host", "" };
        Property<unsigned int> port { this, "port", 1883 };
        Property<String> clientId { this, "clientId", "" };
        Property<size_t> queueSize { this, "queueSize", 16 };
        ArrayProperty<String> serverCert { this, "serverCert" };
        ArrayProperty<String> clientCert { this, "clientCert" };
        ArrayProperty<String> clientKey { this, "clientKey" };
    };

    MqttDriver(
        State& networkReady,
        MdnsDriver& mdns,
        const Config& config,
        const String& instanceName,
        StateSource& mqttReady)
        : networkReady(networkReady)
        , mdns(mdns)
        , config(config)
        , instanceName(instanceName)
        , clientId(getClientId(config.clientId.get(), instanceName))
        , mqttReady(mqttReady) {
        Task::run("mqtt:init", 4096, [this](Task& task) {
            setup();
            Task::loop("mqtt", 8192, [this](Task& task) {
                auto delay = loopAndDelay();
                task.delay(delay);
            });
        });
    }

    shared_ptr<MqttRoot> forRoot(const String& topic) {
        return make_shared<MqttRoot>(*this, topic);
    }

private:
    PublishStatus publish(const String& topic, const JsonDocument& json, Retention retain, QoS qos, ticks timeout = ticks::zero()) {
#ifdef DUMP_MQTT
        String serializedJson;
        serializeJsonPretty(json, serializedJson);
        Log.infoln("MQTT: Queuing topic '%s'%s (qos = %d): %s",
            topic.c_str(), (retain == Retention::Retain ? " (retain)" : ""), qos, serializedJson.c_str());
#endif
        return executeAndAwait(timeout, [&](TaskHandle_t waitingTask) {
            return publishQueue.offerIn(MQTT_QUEUE_TIMEOUT, topic, json, retain, qos, waitingTask);
        });
    }

    PublishStatus clear(const String& topic, Retention retain, QoS qos, ticks timeout = ticks::zero()) {
        Log.traceln("MQTT: Clearing topic '%s'",
            topic.c_str());
        return executeAndAwait(timeout, [&](TaskHandle_t waitingTask) {
            return publishQueue.offerIn(MQTT_QUEUE_TIMEOUT, topic, "", retain, qos, waitingTask);
        });
    }

    PublishStatus executeAndAwait(ticks timeout, std::function<bool(TaskHandle_t)> enqueue) {
        TaskHandle_t waitingTask = timeout == ticks::zero() ? nullptr : xTaskGetCurrentTaskHandle();
        bool offered = enqueue(waitingTask);
        if (!offered) {
            return PublishStatus::QueueFull;
        }
        if (waitingTask == nullptr) {
            return PublishStatus::Pending;
        }
        uint32_t status = ulTaskNotifyTake(pdTRUE, timeout.count());
        switch (status) {
            case 0:
                return PublishStatus::TimeOut;
            case Message::PUBLISH_SUCCESS:
                return PublishStatus::Success;
            case Message::PUBLISH_FAILED:
            default:
                return PublishStatus::Failed;
        }
    }

    /**
     * @brief Subscribes to the given topic.
     *
     * Note that subscription does not support wildcards.
     */
    bool subscribe(const String& topic, QoS qos, SubscriptionHandler handler) {
        // Allow some time for the queue to empty
        return subscribeQueue.offerIn(MQTT_QUEUE_TIMEOUT, topic, qos, handler);
    }

    void setup() {
        // TODO Figure out the right keep alive value
        mqttClient.setKeepAliveTimeout(180);
    }

    String joinStrings(std::list<String> strings) {
        if (strings.empty()) {
            return "";
        }
        String result;
        for (auto& str : strings) {
            result += str + "\n";
        }
        return result;
    }

    ticks loopAndDelay() {
        networkReady.awaitSet();

        if (!mqttClient.isConnected()) {
            Log.infoln("MQTT: Disconnected, connecting");
            mqttReady.clear();

            String serverCert;
            String clientCert;
            String clientKey;

            MdnsRecord mqttServer;
            if (config.host.get().length() > 0) {
                mqttServer.hostname = config.host.get();
                mqttServer.port = config.port.get();
                if (config.serverCert.hasValue()) {
                    serverCert = joinStrings(config.serverCert.get());
                    clientCert = joinStrings(config.clientCert.get());
                    clientKey = joinStrings(config.clientKey.get());
                }
            } else {
                // TODO Handle lookup failure
                mdns.lookupService("mqtt", "tcp", mqttServer, trustMdnsCache);
            }

            String hostname;
            if (mqttServer.ip == IPAddress()) {
                hostname = mqttServer.hostname;
            } else {
                hostname = mqttServer.ip.toString();
            }

            if (serverCert.isEmpty()) {
                Log.infoln("MQTT: server: %s:%d, client ID is '%s'",
                    hostname.c_str(), mqttServer.port, clientId.c_str());
                wifiClient.connect(mqttServer.hostname.c_str(), mqttServer.port);
                mqttClient.begin(wifiClient);
            } else {
                Log.infoln("MQTT: server: %s:%d, client ID is '%s', using TLS",
                    hostname.c_str(), mqttServer.port, clientId.c_str());
                Log.infoln("Server cert: %s", serverCert.c_str());
                Log.infoln("Client cert: %s", clientCert.c_str());
                wifiClientSecure.setCACert(serverCert.c_str());
                wifiClientSecure.setCertificate(clientCert.c_str());
                wifiClientSecure.setPrivateKey(clientKey.c_str());
                wifiClientSecure.connect(mqttServer.hostname.c_str(), mqttServer.port);
                mqttClient.begin(wifiClientSecure);
            }

            if (!mqttClient.connect(clientId.c_str())) {
                Log.errorln("MQTT: Connection failed, error = %d",
                    mqttClient.getLastError());
                trustMdnsCache = false;
                // TODO Implement exponential backoff
                return MQTT_DISCONNECTED_CHECK_INTERVAL;
            } else {
                trustMdnsCache = true;
            }

            // Re-subscribe to existing subscriptions
            for (auto& subscription : subscriptions) {
                registerSubscriptionWithMqtt(subscription);
            }

            Log.infoln("MQTT: Connected");
            mqttReady.set();
        }

        processPublishQueue();
        processSubscriptionQueue();

        return MQTT_LOOP_INTERVAL;
    }

    void processPublishQueue() {
        // Process incoming network traffic
        mqttClient.update();

        publishQueue.drain([&](const Message& message) {
            bool success = mqttClient.publish(message.topic, message.payload, message.retain == Retention::Retain, static_cast<int>(message.qos));
#ifdef DUMP_MQTT
            Log.infoln("MQTT: Published to '%s' (size: %d)",
                message.topic.c_str(), message.payload.length());
#endif
            if (!success) {
                Log.errorln("MQTT: Error publishing to '%s', error = %d",
                    message.topic.c_str(), mqttClient.getLastError());
            }
            if (message.waitingTask != nullptr) {
                uint32_t status = success ? Message::PUBLISH_SUCCESS : Message::PUBLISH_FAILED;
                xTaskNotify(message.waitingTask, status, eSetValueWithOverwrite);
            }
        });
    }

    void processSubscriptionQueue() {
        subscribeQueue.drain([&](const Subscription& subscription) {
            if (registerSubscriptionWithMqtt(subscription)) {
                subscriptions.push_back(subscription);
            }
        });
    }

    // Actually subscribe to the given topic
    bool registerSubscriptionWithMqtt(const Subscription& subscription) {
        Log.infoln("MQTT: Subscribing to topic '%s' (qos = %d)",
            subscription.topic.c_str(), subscription.qos);
        bool success = mqttClient.subscribe(subscription.topic, static_cast<int>(subscription.qos), [subscription](const String& payload, const size_t size) {
            if (payload.isEmpty()) {
                Log.verboseln("MQTT: Ignoring empty payload");
                return;
            }

#ifdef DUMP_MQTT
            Log.infoln("MQTT: Received '%s' (size: %d): %s",
                subscription.topic.c_str(), size, payload.c_str());
#else
            Log.traceln("MQTT: Received '%s' (size: %d)",
                subscription.topic.c_str(), size);
#endif

            DynamicJsonDocument json(docSizeFor(payload));
            deserializeJson(json, payload);
            subscription.handle(subscription.topic, json.as<JsonObject>());
        });
        if (!success) {
            Log.errorln("MQTT: Error subscribing to topic '%s', error = %d\n",
                subscription.topic.c_str(), mqttClient.getLastError());
        }
        return success;
    }

    static String getClientId(const String& clientId, const String& instanceName) {
        if (clientId.length() > 0) {
            return clientId;
        }
        return "ugly-duckling-" + instanceName;
    }

    State& networkReady;
    WiFiClient wifiClient;
    WiFiClientSecure wifiClientSecure;
    MdnsDriver& mdns;
    bool trustMdnsCache = true;
    const Config& config;
    const String instanceName;

    const String clientId;

    StateSource& mqttReady;

    static constexpr int MQTT_BUFFER_SIZE = 2048;
    MQTTPubSub::PubSubClient<MQTT_BUFFER_SIZE> mqttClient;

    Queue<Message> publishQueue { "mqtt-publish", config.queueSize.get() };
    Queue<Subscription> subscribeQueue { "mqtt-subscribe", config.queueSize.get() };
    // TODO Use a map instead
    std::list<Subscription> subscriptions;

    // TODO Review these values
    static constexpr milliseconds MQTT_LOOP_INTERVAL = seconds(1);
    static constexpr milliseconds MQTT_DISCONNECTED_CHECK_INTERVAL = seconds(1);
    static constexpr milliseconds MQTT_QUEUE_TIMEOUT = seconds(1);
};

}    // namespace farmhub::kernel::drivers
