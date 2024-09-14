#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <variant>

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

using namespace std::chrono_literals;
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

    enum class LogPublish {
        Log,
        Silent
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

        PublishStatus publish(const String& suffix, const JsonDocument& json, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = ticks::zero(), LogPublish log = LogPublish::Log) {
            return mqtt.publish(fullTopic(suffix), json, retain, qos, timeout, log);
        }

        PublishStatus publish(const String& suffix, std::function<void(JsonObject&)> populate, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = ticks::zero(), LogPublish log = LogPublish::Log) {
            JsonDocument doc;
            JsonObject root = doc.to<JsonObject>();
            populate(root);
            return publish(suffix, doc, retain, qos, timeout, log);
        }

        PublishStatus clear(const String& suffix, Retention retain = Retention::NoRetain, QoS qos = QoS::AtMostOnce, ticks timeout = ticks::zero()) {
            return mqtt.clear(fullTopic(suffix), retain, qos, timeout);
        }

        bool subscribe(const String& suffix, SubscriptionHandler handler) {
            return subscribe(suffix, QoS::ExactlyOnce, handler);
        }

        bool registerCommand(const String& name, CommandHandler handler) {
            String suffix = "commands/" + name;
            return subscribe(suffix, QoS::ExactlyOnce, [this, name, suffix, handler](const String&, const JsonObject& request) {
                // TODO Do exponential backoff when clear cannot be finished
                // Clear topic and wait for it to be cleared
                auto clearStatus = mqtt.clear(fullTopic(suffix), Retention::Retain, QoS::ExactlyOnce, std::chrono::seconds { 5 });
                if (clearStatus != PublishStatus::Success) {
                    Log.error("MQTT: Failed to clear retained command topic '%s', status: %d", suffix.c_str(), static_cast<int>(clearStatus));
                }

                JsonDocument responseDoc;
                auto response = responseDoc.to<JsonObject>();
                handler(request, response);
                if (response.size() > 0) {
                    publish("responses/" + name, responseDoc, Retention::NoRetain, QoS::ExactlyOnce);
                }
            });
        }

        void registerCommand(Command& command) {
            registerCommand(command.name, [&](const JsonObject& request, JsonObject& response) {
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
    struct OutgoingMessage {
        String topic;
        String payload;
        Retention retain;
        QoS qos;
        TaskHandle_t waitingTask;
        LogPublish log;

        static const uint32_t PUBLISH_SUCCESS = 1;
        static const uint32_t PUBLISH_FAILED = 2;

        OutgoingMessage(const String& topic, const String& payload, Retention retention, QoS qos, TaskHandle_t waitingTask, LogPublish log)
            : topic(topic)
            , payload(payload)
            , retain(retention)
            , qos(qos)
            , waitingTask(waitingTask)
            , log(log) {
        }
    };

    struct IncomingMessage {
        String topic;
        String payload;

        IncomingMessage(const String& topic, const String& payload)
            : topic(topic)
            , payload(payload) {
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
            : Subscription(other.topic, other.qos, other.handle) {
        }
    };

public:
    class Config : public ConfigurationSection {
    public:
        Property<String> host { this, "host", "" };
        Property<unsigned int> port { this, "port", 1883 };
        Property<String> clientId { this, "clientId", "" };
        Property<size_t> queueSize { this, "queueSize", 128 };
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
        , clientId(getClientId(config.clientId.get(), instanceName))
        , mqttReady(mqttReady) {
        Task::run("mqtt:init", 4096, [this](Task& task) {
            setup();
            Task::run("mqtt", 4096, [this](Task& task) {
                runEventLoop(task);
            });
            Task::loop("mqtt:incoming", 4096, [this](Task& task) {
                incomingQueue.take([&](const IncomingMessage& message) {
                    processIncomingMessage(message);
                });
            });
        });
    }

    shared_ptr<MqttRoot> forRoot(const String& topic) {
        return make_shared<MqttRoot>(*this, topic);
    }

private:
    PublishStatus publish(const String& topic, const JsonDocument& json, Retention retain, QoS qos, ticks timeout = ticks::zero(), LogPublish log = LogPublish::Log) {
#ifdef DUMP_MQTT
        if (log == LogPublish::Log) {
            String serializedJson;
            serializeJsonPretty(json, serializedJson);
            Log.debug("MQTT: Queuing topic '%s'%s (qos = %d): %s",
                topic.c_str(), (retain == Retention::Retain ? " (retain)" : ""), static_cast<int>(qos), serializedJson.c_str());
        }
#endif
        String payload;
        serializeJson(json, payload);
        return executeAndAwait(timeout, [&](TaskHandle_t waitingTask) {
            return outgoingQueue.offerIn(MQTT_QUEUE_TIMEOUT, OutgoingMessage { topic, payload, retain, qos, waitingTask, log });
        });
    }

    PublishStatus clear(const String& topic, Retention retain, QoS qos, ticks timeout = ticks::zero()) {
        Log.debug("MQTT: Clearing topic '%s'",
            topic.c_str());
        return executeAndAwait(timeout, [&](TaskHandle_t waitingTask) {
            return outgoingQueue.offerIn(MQTT_QUEUE_TIMEOUT, OutgoingMessage { topic, "", retain, qos, waitingTask, LogPublish::Log });
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
            case OutgoingMessage::PUBLISH_SUCCESS:
                return PublishStatus::Success;
            case OutgoingMessage::PUBLISH_FAILED:
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
        return outgoingQueue.offerIn(MQTT_QUEUE_TIMEOUT, Subscription { topic, qos, handler });
    }

    void setup() {
        // TODO Figure out the right keep alive value
        mqttClient.setKeepAliveTimeout(180);
        mqttClient.subscribe([this](const String& topic, const String& payload, const size_t size) {
            incomingQueue.offerIn(MQTT_QUEUE_TIMEOUT, topic, payload);
        });
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

    void runEventLoop(Task& task) {
        // TODO Extract constant
        time_point<system_clock> alertUntil = system_clock::now() + 5s;

        while (true) {
            auto timeout = alertUntil - system_clock::now();
            if (timeout <= 0ns) {
                timeout = MQTT_MAX_TIMEOUT;
            } else {
                timeout = std::min(timeout, MQTT_LOOP_INTERVAL);
            }
            Log.trace("MQTT: Waiting for event for %lld ms", duration_cast<milliseconds>(timeout).count());
            outgoingQueue.pollIn(duration_cast<ticks>(timeout), [&](const auto& event) {
                Log.trace("MQTT: Processing event");
                ensureConnected(task);

                std::visit(
                    [this](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, OutgoingMessage>) {
                            Log.trace("MQTT: Processing outgoing message");
                            processOutgoingMessage(arg);
                        } else if constexpr (std::is_same_v<T, Subscription>) {
                            Log.trace("MQTT: Processing subscription");
                            processSubscription(arg);
                        }
                    },
                    event);

                // TODO Extract constant
                alertUntil = std::max(alertUntil, system_clock::now() + 5s);
            });

            if (alertUntil > system_clock::now()) {
                ensureConnected(task);
                // Process incoming network traffic
                mqttClient.update();
            } else {
                Log.debug("MQTT: Not alert anymore, disconnecting");
                mqttClient.disconnect();
            }
        }
    }

    void ensureConnected(Task& task) {
        while (!connectIfNecessary()) {
            // Do exponential backoff
            task.delayUntil(MQTT_DISCONNECTED_CHECK_INTERVAL);
        }
    }

    bool connectIfNecessary() {
        networkReady.awaitSet();

        if (!mqttClient.isConnected()) {
            Log.debug("MQTT: Connecting to MQTT server");
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
                Log.debug("MQTT: server: %s:%d, client ID is '%s'",
                    hostname.c_str(), mqttServer.port, clientId.c_str());
                wifiClient.connect(hostname.c_str(), mqttServer.port);
                mqttClient.begin(wifiClient);
            } else {
                Log.debug("MQTT: server: %s:%d, client ID is '%s', using TLS",
                    hostname.c_str(), mqttServer.port, clientId.c_str());
                Log.debug("Server cert: %s", serverCert.c_str());
                Log.debug("Client cert: %s", clientCert.c_str());
                wifiClientSecure.setCACert(serverCert.c_str());
                wifiClientSecure.setCertificate(clientCert.c_str());
                wifiClientSecure.setPrivateKey(clientKey.c_str());
                wifiClientSecure.connect(mqttServer.hostname.c_str(), mqttServer.port);
                mqttClient.begin(wifiClientSecure);
            }

            if (!mqttClient.connect(clientId.c_str())) {
                Log.error("MQTT: Connection failed, error = %d",
                    mqttClient.getLastError());
                trustMdnsCache = false;
                return false;
            } else {
                trustMdnsCache = true;
            }

            // Re-subscribe to existing subscriptions
            for (auto& subscription : subscriptions) {
                registerSubscriptionWithMqtt(subscription);
            }

            Log.debug("MQTT: Connected");
            mqttReady.set();
        }
        return true;
    }

    void processOutgoingMessage(const OutgoingMessage message) {
        bool success = mqttClient.publish(message.topic, message.payload, message.retain == Retention::Retain, static_cast<int>(message.qos));
#ifdef DUMP_MQTT
        if (message.log == LogPublish::Log) {
            Log.trace("MQTT: Published to '%s' (size: %d)",
                message.topic.c_str(), message.payload.length());
        }
#endif
        if (!success) {
            Log.trace("MQTT: Error publishing to '%s', error = %d",
                message.topic.c_str(), mqttClient.getLastError());
        }
        if (message.waitingTask != nullptr) {
            uint32_t status = success ? OutgoingMessage::PUBLISH_SUCCESS : OutgoingMessage::PUBLISH_FAILED;
            xTaskNotify(message.waitingTask, status, eSetValueWithOverwrite);
        }
    }

    void processSubscription(const Subscription& subscription) {
        if (registerSubscriptionWithMqtt(subscription)) {
            subscriptions.push_back(subscription);
        }
    }

    void processIncomingMessage(const IncomingMessage& message) {
        const String& topic = message.topic;
        const String& payload = message.payload;

        if (payload.isEmpty()) {
            Log.trace("MQTT: Ignoring empty payload");
            return;
        }

#ifdef DUMP_MQTT
        Log.debug("MQTT: Received '%s' (size: %d): %s",
            topic.c_str(), payload.length(), payload.c_str());
#else
        Log.debug("MQTT: Received '%s' (size: %d)",
            topic.c_str(), payload.length());
#endif
        for (auto subscription : subscriptions) {
            if (subscription.topic == topic) {
                JsonDocument json;
                deserializeJson(json, payload);
                // TODO Make timeout and stack size configurable
                auto result = Task::runIn("mqtt:incoming-handler", 10s, 4096, [&](Task& task) {
                    subscription.handle(topic, json.as<JsonObject>());
                });
                if (result != Task::RunResult::OK) {
                    Log.error("MQTT: Incoming handler for topic '%s' timed out",
                        topic.c_str());
                }
                return;
            }
        }
        Log.warn("MQTT: No handler for topic '%s'",
            topic.c_str());
    }

    // Actually subscribe to the given topic
    bool registerSubscriptionWithMqtt(const Subscription& subscription) {
        Log.debug("MQTT: Subscribing to topic '%s' (qos = %d)",
            subscription.topic.c_str(), static_cast<int>(subscription.qos));
        bool success = mqttClient.subscribe(subscription.topic, static_cast<int>(subscription.qos), [](const String& payload, const size_t size) {
            // Global handler will take care of putting the received message on the incoming queue
        });
        if (!success) {
            Log.error("MQTT: Error subscribing to topic '%s', error = %d\n",
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

    const String clientId;

    StateSource& mqttReady;

    static constexpr int MQTT_BUFFER_SIZE = 2048;
    MQTTPubSub::PubSubClient<MQTT_BUFFER_SIZE> mqttClient;

    Queue<std::variant<OutgoingMessage, Subscription>> outgoingQueue { "mqtt-outgoing", config.queueSize.get() };
    Queue<IncomingMessage> incomingQueue { "mqtt-incoming", config.queueSize.get() };
    // TODO Use a map instead
    std::list<Subscription> subscriptions;

    // TODO Review these values
    static constexpr nanoseconds MQTT_LOOP_INTERVAL = 1s;
    static constexpr milliseconds MQTT_DISCONNECTED_CHECK_INTERVAL = 1s;
    static constexpr milliseconds MQTT_QUEUE_TIMEOUT = 1s;

    static constexpr milliseconds MQTT_MAX_TIMEOUT = 1h;
};

}    // namespace farmhub::kernel::drivers
