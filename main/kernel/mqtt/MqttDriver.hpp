#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>

#include <esp_event.h>
#include <mqtt_client.h>

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

using namespace farmhub::kernel::drivers;

namespace farmhub::kernel::mqtt {

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

class MqttRoot;

class MqttDriver {
private:
    struct OutgoingMessage {
        const String topic;
        const String payload;
        const Retention retain;
        const QoS qos;
        const TaskHandle_t waitingTask;
        const LogPublish log;
        const milliseconds extendAlert;

        static const uint32_t PUBLISH_SUCCESS = 1;
        static const uint32_t PUBLISH_FAILED = 2;
    };

    struct IncomingMessage {
        const String topic;
        const String payload;
    };

    struct Subscription {
        const String topic;
        const QoS qos;
        const SubscriptionHandler handle;
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
        WiFiDriver& wifi,
        MdnsDriver& mdns,
        const Config& config,
        const String& instanceName,
        bool powerSaveMode,
        StateSource& mqttReady)
        : wifi(wifi)
        , mdns(mdns)
        , configHostname(config.host.get())
        , configPort(config.port.get())
        , configServerCert(joinStrings(config.serverCert.get()))
        , configClientCert(joinStrings(config.clientCert.get()))
        , configClientKey(joinStrings(config.clientKey.get()))
        , clientId(getClientId(config.clientId.get(), instanceName))
        , powerSaveMode(powerSaveMode)
        , mqttReady(mqttReady)
        , outgoingQueue("mqtt-outgoing", config.queueSize.get())
        , incomingQueue("mqtt-incoming", config.queueSize.get()) {

        Task::run("mqtt", 5120, [this](Task& task) {
            runEventLoop(task);
        });
        Task::loop("mqtt:incoming", 4096, [this](Task& task) {
            incomingQueue.take([&](const IncomingMessage& message) {
                processIncomingMessage(message);
            });
        });
    }

    shared_ptr<MqttRoot> forRoot(const String& topic) {
        return make_shared<MqttRoot>(*this, topic);
    }

private:
    PublishStatus publish(const String& topic, const JsonDocument& json, Retention retain, QoS qos, ticks timeout = ticks::zero(), LogPublish log = LogPublish::Log, milliseconds extendAlert = MQTT_ALERT_AFTER_OUTGOING) {
#ifdef DUMP_MQTT
        if (log == LogPublish::Log) {
            String serializedJson;
            serializeJsonPretty(json, serializedJson);
            LOGTD("mqtt", "Queuing topic '%s'%s (qos = %d): %s",
                topic.c_str(), (retain == Retention::Retain ? " (retain)" : ""), static_cast<int>(qos), serializedJson.c_str());
        }
#endif
        String payload;
        serializeJson(json, payload);
        // Stay alert until the message is sent
        extendAlert = std::max(duration_cast<milliseconds>(timeout), extendAlert);
        return executeAndAwait(timeout, [&](TaskHandle_t waitingTask) {
            return outgoingQueue.offerIn(MQTT_QUEUE_TIMEOUT, OutgoingMessage { topic, payload, retain, qos, waitingTask, log, extendAlert });
        });
    }

    PublishStatus clear(const String& topic, Retention retain, QoS qos, ticks timeout = ticks::zero(), milliseconds extendAlert = MQTT_ALERT_AFTER_OUTGOING) {
        LOGTD("mqtt", "Clearing topic '%s'",
            topic.c_str());
        return executeAndAwait(timeout, [&](TaskHandle_t waitingTask) {
            return outgoingQueue.offerIn(MQTT_QUEUE_TIMEOUT, OutgoingMessage { topic, "", retain, qos, waitingTask, LogPublish::Log, extendAlert });
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
            case 0: {
                Lock lock(pendingMessagesMutex);
                pendingMessages.remove_if([waitingTask](const auto& message) {
                    return message.waitingTask == waitingTask;
                });
                return PublishStatus::TimeOut;
            }
            case OutgoingMessage::PUBLISH_SUCCESS: {
                return PublishStatus::Success;
            }
            case OutgoingMessage::PUBLISH_FAILED:
            default: {
                return PublishStatus::Failed;
            }
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
            milliseconds timeout;
            if (powerSaveMode) {
                timeout = duration_cast<milliseconds>(alertUntil - system_clock::now());
                if (timeout > milliseconds::zero()) {
                    LOGTV("mqtt", "Alert for another %lld ms, checking for incoming messages",
                        timeout.count());
                    ensureConnected(task);
                    timeout = std::min(timeout, MQTT_LOOP_INTERVAL);
                } else if (!pendingMessages.empty()) {
                    LOGTV("mqtt", "Alert expired, but there are pending messages, staying connected");
                    ensureConnected(task);
                    timeout = MQTT_LOOP_INTERVAL;
                } else if (powerSaveMode) {
                    LOGTV("mqtt", "Not alert anymore, disconnecting");
                    disconnect();
                    timeout = MQTT_MAX_TIMEOUT_POWER_SAVE;
                }
            } else {
                LOGTV("mqtt", "Power save mode not enabled, staying connected");
                ensureConnected(task);
                timeout = MQTT_LOOP_INTERVAL;
            }

            // LOGTV("mqtt", "Waiting for outgoing event for %lld ms", duration_cast<milliseconds>(timeout).count());
            outgoingQueue.pollIn(duration_cast<ticks>(timeout), [&](const auto& event) {
                LOGTV("mqtt", "Processing outgoing event");
                ensureConnected(task);

                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, OutgoingMessage>) {
                            LOGTV("mqtt", "Processing outgoing message: %s",
                                arg.topic.c_str());
                            processOutgoingMessage(arg);
                            alertUntil = std::max(alertUntil, system_clock::now() + arg.extendAlert);
                        } else if constexpr (std::is_same_v<T, Subscription>) {
                            LOGTV("mqtt", "Processing subscription");
                            processSubscription(arg);
                            alertUntil = std::max(alertUntil, system_clock::now() + MQTT_ALERT_AFTER_OUTGOING);
                        }
                    },
                    event);
            });
        }
    }

    void ensureConnected(Task& task) {
        while (!connectIfNecessary()) {
            // Do exponential backoff
            task.delayUntil(MQTT_DISCONNECTED_CHECK_INTERVAL);
        }
    }

    void disconnect() {
        if (client != nullptr) {
            LOGTD("mqtt", "Disconnecting from MQTT server");
            mqttReady.clear();
            ESP_ERROR_CHECK(esp_mqtt_client_disconnect(client));
            stopMqttClient();
            wifiConnection.reset();
        }
    }

    void stopMqttClient() {
        if (client != nullptr) {
            ESP_ERROR_CHECK(esp_mqtt_client_stop(client));
            destroyMqttClient();
        }
    }

    void destroyMqttClient() {
        if (client != nullptr) {
            ESP_ERROR_CHECK(esp_mqtt_client_destroy(client));
            client = nullptr;
        }
    }

    bool connectIfNecessary() {
        if (!wifiConnection.has_value()) {
            LOGTV("mqtt", "Connecting to WiFi...");
            wifiConnection.emplace(wifi);
            LOGTV("mqtt", "Connected to WiFi");
        }

        if (!mqttReady.isSet()) {
            if (client == nullptr) {
                LOGTD("mqtt", "Connecting to MQTT server");
                String hostname;
                uint32_t port;
                if (configHostname.isEmpty()) {
#ifdef WOKWI
                    hostname = "host.wokwi.internal";
                    port = 1883;
#else
                    MdnsRecord mqttServer;
                    if (!mdns.lookupService("mqtt", "tcp", mqttServer, trustMdnsCache)) {
                        LOGTE("mqtt", "Failed to lookup MQTT server");
                        return false;
                    }
                    hostname = mqttServer.ip == IPAddress()
                        ? mqttServer.hostname
                        : mqttServer.ip.toString();
                    port = mqttServer.port;
#endif
                } else {
                    hostname = configHostname;
                    port = configPort;
                }

                esp_mqtt_client_config_t config = {
                    .broker {
                        .address {
                            .hostname = hostname.c_str(),
                            .port = port,
                        },
                    },
                    .credentials {
                        .client_id = clientId.c_str(),
                    },
                    .network {
                        .timeout_ms = duration_cast<milliseconds>(10s).count(),
                    },
                };

                LOGTD("mqtt", "server: %s:%ld, client ID is '%s'",
                    config.broker.address.hostname,
                    config.broker.address.port,
                    config.credentials.client_id);

                if (!configServerCert.isEmpty()) {
                    config.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
                    config.broker.verification.certificate = configServerCert.c_str();
                    LOGTV("mqtt", "Server cert:\n%s",
                        config.broker.verification.certificate);

                    if (!configClientCert.isEmpty() && !configClientKey.isEmpty()) {
                        config.credentials.authentication = {
                            .certificate = configClientCert.c_str(),
                            .key = configClientKey.c_str(),
                        };
                        LOGTV("mqtt", "Client cert:\n%s",
                            config.credentials.authentication.certificate);
                    }
                } else {
                    config.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
                }

                client = esp_mqtt_client_init(&config);

                ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, handleMqttEventCallback, this));

                esp_err_t err = esp_mqtt_client_start(client);
                if (err != ESP_OK) {
                    LOGTE("mqtt", "Connection failed, error = 0x%x: %s",
                        err, esp_err_to_name(err));
                    trustMdnsCache = false;
                    destroyMqttClient();
                    return false;
                } else {
                    trustMdnsCache = true;
                }
            } else {
                // TODO Reconnection probably doesn't work like this?
                LOGTD("mqtt", "Reconnecting to MQTT server");
                ESP_ERROR_CHECK(esp_mqtt_client_reconnect(client));
            }
            if (!mqttReady.awaitSet(MQTT_CONNECTION_TIMEOUT)) {
                LOGTD("mqtt", "Connecting to MQTT server timed out");
                stopMqttClient();
                return false;
            }

            // Re-subscribe to existing subscriptions
            for (auto& subscription : subscriptions) {
                registerSubscriptionWithMqtt(subscription);
            }

            LOGTD("mqtt", "Connected");
        }
        return true;
    }

    static void handleMqttEventCallback(void* userData, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
        auto event = static_cast<esp_mqtt_event_handle_t>(eventData);
        // LOGTV("mqtt", "Event dispatched from event loop: base=%s, event_id=%d, client=%p, data=%p, data_len=%d, topic=%p, topic_len=%d, msg_id=%d",
        //     eventBase, event->event_id, event->client, event->data, event->data_len, event->topic, event->topic_len, event->msg_id);
        auto* driver = static_cast<MqttDriver*>(userData);
        driver->handleMqttEvent(eventId, event);
    }

    void handleMqttEvent(int eventId, esp_mqtt_event_handle_t event) {
        switch (eventId) {
            case MQTT_EVENT_BEFORE_CONNECT: {
                LOGTV("mqtt", "Connecting to MQTT server");
                break;
            }
            case MQTT_EVENT_CONNECTED: {
                LOGTV("mqtt", "Connected to MQTT server");
                mqttReady.set();
                break;
            }
            case MQTT_EVENT_DISCONNECTED: {
                LOGTV("mqtt", "Disconnected from MQTT server");
                mqttReady.clear();
                Lock lock(pendingMessagesMutex);
                pendingMessages.clear();
                break;
            }
            case MQTT_EVENT_SUBSCRIBED: {
                LOGTV("mqtt", "Subscribed, message ID: %d", event->msg_id);
                break;
            }
            case MQTT_EVENT_UNSUBSCRIBED: {
                LOGTV("mqtt", "Unsubscribed, message ID: %d", event->msg_id);
                break;
            }
            case MQTT_EVENT_PUBLISHED: {
                LOGTV("mqtt", "Published, message ID %d", event->msg_id);
                notifyPendingTask(event, true);
                break;
            }
            case MQTT_EVENT_DATA: {
                String topic(event->topic, event->topic_len);
                String payload(event->data, event->data_len);
                LOGTV("mqtt", "Received message on topic '%s'",
                    topic.c_str());
                incomingQueue.offerIn(MQTT_QUEUE_TIMEOUT, IncomingMessage { topic, payload });
                break;
            }
            case MQTT_EVENT_ERROR: {
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                    LOGTE("mqtt", "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
                    logErrorIfNonZero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                    logErrorIfNonZero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                    logErrorIfNonZero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
                }
                notifyPendingTask(event, false);
                break;
            }
            default: {
                LOGTW("mqtt", "Unknown event %d", eventId);
                break;
            }
        }
    }

    void notifyPendingTask(esp_mqtt_event_handle_t event, bool success) {
        Lock lock(pendingMessagesMutex);
        pendingMessages.remove_if([&](const auto& message) {
            if (message.messageId == event->msg_id) {
                notifyWaitingTask(message.waitingTask, success);
                return true;
            } else {
                return false;
            }
        });
    }

    void notifyWaitingTask(TaskHandle_t task, bool success) {
        if (task != nullptr) {
            uint32_t status = success ? OutgoingMessage::PUBLISH_SUCCESS : OutgoingMessage::PUBLISH_FAILED;
            xTaskNotify(task, status, eSetValueWithOverwrite);
        }
    }

    static void logErrorIfNonZero(const char* message, int error) {
        if (error != 0) {
            LOGTE("mqtt", " - %s: 0x%x", message, error);
        }
    }

    void processOutgoingMessage(const OutgoingMessage message) {
        Lock lock(pendingMessagesMutex);
        int ret = esp_mqtt_client_enqueue(
            client,
            message.topic.c_str(),
            message.payload.c_str(),
            message.payload.length(),
            static_cast<int>(message.qos),
            message.retain == Retention::Retain,
            true);
#ifdef DUMP_MQTT
        if (message.log == LogPublish::Log) {
            LOGTV("mqtt", "Published to '%s' (size: %d), result: %d",
                message.topic.c_str(), message.payload.length(), ret);
        }
#endif
        switch (ret) {
            case -1: {
                LOGTD("mqtt", "Error publishing to '%s'",
                    message.topic.c_str());
                notifyWaitingTask(message.waitingTask, false);
                break;
            }
            case -2: {
                LOGTD("mqtt", "Outbox full, message to '%s' dropped",
                    message.topic.c_str());
                notifyWaitingTask(message.waitingTask, false);
                break;
            }
            default: {
                auto messageId = ret;
                if (message.waitingTask != nullptr) {
                    if (messageId == 0) {
                        // Notify tasks waiting on QoS 0 messages immediately
                        notifyWaitingTask(message.waitingTask, true);
                    } else {
                        // Record
                        pendingMessages.push_back({ messageId, message.waitingTask });
                    }
                    break;
                }
            }
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
            LOGTV("mqtt", "Ignoring empty payload");
            return;
        }

#ifdef DUMP_MQTT
        LOGTD("mqtt", "Received '%s' (size: %d): %s",
            topic.c_str(), payload.length(), payload.c_str());
#else
        LOGTD("mqtt", "Received '%s' (size: %d)",
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
                    LOGTE("mqtt", "Incoming handler for topic '%s' timed out",
                        topic.c_str());
                }
                return;
            }
        }
        LOGTW("mqtt", "No handler for topic '%s'",
            topic.c_str());
    }

    // Actually subscribe to the given topic
    bool registerSubscriptionWithMqtt(const Subscription& subscription) {
        LOGTV("mqtt", "Subscribing to topic '%s' (qos = %d)",
            subscription.topic.c_str(), static_cast<int>(subscription.qos));
        int ret = esp_mqtt_client_subscribe(client, subscription.topic.c_str(), static_cast<int>(subscription.qos));
        switch (ret) {
            case -1:
                LOGTE("mqtt", "Error subscribing to topic '%s'",
                    subscription.topic.c_str());
                return false;
            case -2:
                LOGTE("mqtt", "Subscription to topic '%s' failed, outbox full",
                    subscription.topic.c_str());
                return false;
            default:
                return true;
        }
    }

    static String getClientId(const String& clientId, const String& instanceName) {
        if (clientId.length() > 0) {
            return clientId;
        }
        return "ugly-duckling-" + instanceName;
    }

    WiFiDriver& wifi;
    std::optional<WiFiConnection> wifiConnection;
    MdnsDriver& mdns;
    bool trustMdnsCache = true;

    const String configHostname;
    const int configPort;
    const String configServerCert;
    const String configClientCert;
    const String configClientKey;
    const String clientId;

    const bool powerSaveMode;
    StateSource& mqttReady;

    esp_mqtt_client_handle_t client = nullptr;

    Queue<std::variant<OutgoingMessage, Subscription>> outgoingQueue;
    Queue<IncomingMessage> incomingQueue;
    // TODO Use a map instead
    std::list<Subscription> subscriptions;

    struct PendingMessage {
        const int messageId;
        const TaskHandle_t waitingTask;
    };
    Mutex pendingMessagesMutex;
    std::list<PendingMessage> pendingMessages;

    // TODO Review these values
    static constexpr milliseconds MQTT_CONNECTION_TIMEOUT = 10s;
    static constexpr milliseconds MQTT_LOOP_INTERVAL = 1s;
    static constexpr milliseconds MQTT_DISCONNECTED_CHECK_INTERVAL = 5s;
    static constexpr milliseconds MQTT_QUEUE_TIMEOUT = 1s;
    static constexpr milliseconds MQTT_ALERT_AFTER_OUTGOING = 1s;
    static constexpr milliseconds MQTT_MAX_TIMEOUT_POWER_SAVE = 1h;

    friend class MqttRoot;
};

}    // namespace farmhub::kernel::mqtt
