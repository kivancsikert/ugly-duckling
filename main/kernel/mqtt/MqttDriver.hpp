#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

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
        , eventQueue("mqtt-outgoing", config.queueSize.get())
        , incomingQueue("mqtt-incoming", config.queueSize.get()) {

        Task::run("mqtt", 5120, [this](Task& task) {
            configMqttClient(mqttConfig);
            client = esp_mqtt_client_init(&mqttConfig);

            ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, handleMqttEventCallback, this));

            runEventLoop(task);
        });
        Task::loop("mqtt:incoming", 4096, [this](Task& task) {
            incomingQueue.take([&](const IncomingMessage& message) {
                processIncomingMessage(message);
            });
        });
    }

    void configMqttClient(esp_mqtt_client_config_t& config) {
        if (configHostname.isEmpty()) {
#ifdef WOKWI
            hostname = "host.wokwi.internal";
            port = 1883;
#else
            MdnsRecord mqttServer;
            while (!mdns.lookupService("mqtt", "tcp", mqttServer, trustMdnsCache)) {
                LOGTE("mqtt", "Failed to lookup MQTT server from mDNS");
                trustMdnsCache = false;
                Task::delay(5s);
            }
            trustMdnsCache = true;
            hostname = mqttServer.ip == IPAddress()
                ? mqttServer.hostname
                : mqttServer.ip.toString();
            port = mqttServer.port;
#endif
        } else {
            hostname = configHostname;
            port = configPort;
        }

        config = {
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
    }

    shared_ptr<MqttRoot> forRoot(const String& topic) {
        return make_shared<MqttRoot>(*this, topic);
    }

private:
    struct PendingMessage {
        const int messageId;
        const TaskHandle_t waitingTask;
        const milliseconds extendKeepAlive;
    };

    struct OutgoingMessage {
        const String topic;
        const String payload;
        const Retention retain;
        const QoS qos;
        const TaskHandle_t waitingTask;
        const LogPublish log;
        const milliseconds extendKeepAlive;
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

    struct MessagePublished {
        const int messageId;
        const bool success;
    };

    struct Connected {
        const bool sessionPresent;
    };

    struct Disconnected { };

    PublishStatus publish(const String& topic, const JsonDocument& json, Retention retain, QoS qos, ticks timeout = ticks::zero(), LogPublish log = LogPublish::Log, milliseconds extendKeepAlive = MQTT_KEEP_ALIVE_AFTER_OUTGOING) {
        if (log == LogPublish::Log) {
#ifdef DUMP_MQTT
            String serializedJson;
            serializeJsonPretty(json, serializedJson);
            LOGTD("mqtt", "Queuing topic '%s'%s (qos = %d, timeout = %lld ms): %s",
                topic.c_str(),
                (retain == Retention::Retain ? " (retain)" : ""),
                static_cast<int>(qos),
                duration_cast<milliseconds>(timeout).count(),
                serializedJson.c_str());
#else
            LOGTV("mqtt", "Queuing topic '%s'%s (qos = %d, timeout = %lld ms)",
                topic.c_str(),
                (retain == Retention::Retain ? " (retain)" : ""),
                static_cast<int>(qos),
                duration_cast<milliseconds>(timeout).count());
#endif
        }
        String payload;
        serializeJson(json, payload);
        return executeAndAwait(timeout, [&](TaskHandle_t waitingTask) {
            return eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, OutgoingMessage { topic, payload, retain, qos, waitingTask, log, extendKeepAlive });
        });
    }

    PublishStatus clear(const String& topic, Retention retain, QoS qos, ticks timeout = ticks::zero(), milliseconds extendKeepAlive = MQTT_KEEP_ALIVE_AFTER_OUTGOING) {
        LOGTD("mqtt", "Clearing topic '%s' (qos = %d, timeout = %lld ms)",
            topic.c_str(),
            static_cast<int>(qos),
            duration_cast<milliseconds>(timeout).count());
        return executeAndAwait(timeout, [&](TaskHandle_t waitingTask) {
            return eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, OutgoingMessage { topic, "", retain, qos, waitingTask, LogPublish::Log, extendKeepAlive });
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

        // Wait for task notification
        uint32_t status = ulTaskNotifyTake(pdTRUE, timeout.count());
        switch (status) {
            case 0:
                return PublishStatus::TimeOut;
            case PUBLISH_SUCCESS:
                return PublishStatus::Success;
            case PUBLISH_FAILED:
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
        return eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, Subscription { topic, qos, handler });
    }

    static String joinStrings(std::list<String> strings) {
        if (strings.empty()) {
            return "";
        }
        String result;
        for (auto& str : strings) {
            result += str + "\n";
        }
        return result;
    }

    enum class MqttState {
        Disconnected,
        Connecting,
        Connected,
    };

    void runEventLoop(Task& task) {
        // How long we need to stay connected after a connection is made
        //   - to await incoming messages
        //   - to publish outgoing QoS = 0 messages
        auto keepAliveAfterConnection = milliseconds::zero();
        auto keepAliveUntil = boot_clock::zero();

        // We are not yet connected
        auto state = MqttState::Disconnected;
        auto connectionStarted = boot_clock::zero();

        // The first session is always clean
        auto startCleanSession = true;

        // List of messages we are waiting on
        std::list<PendingMessage> pendingMessages;

        while (true) {
            auto now = boot_clock::now();
            bool shouldBeConencted =
                // We stay connected all the time if we are not in power save mode
                !powerSaveMode
                // We stay connected if there are pending messages (timeouts will cause them to be removed)
                || !pendingMessages.empty()
                // We stay connected if recent subscriptions or published QoS = 0 messages force us to keep alive
                || keepAliveUntil > now
                || keepAliveAfterConnection > milliseconds::zero();

            if (shouldBeConencted) {
                switch (state) {
                    case MqttState::Disconnected:
                        connect(startCleanSession);
                        state = MqttState::Connecting;
                        connectionStarted = now;
                        break;
                    case MqttState::Connecting:
                        if (now - connectionStarted > MQTT_CONNECTION_TIMEOUT) {
                            LOGTE("mqtt", "Connecting to MQTT server timed out");
                            mqttReady.clear();
                            disconnect();
                            // Make sure we re-lookup the server address when we retry
                            trustMdnsCache = false;
                            state = MqttState::Disconnected;
                        }
                        break;
                    case MqttState::Connected:
                        // Stay connected
                        break;
                }
            } else {
                if (state != MqttState::Disconnected) {
                    mqttReady.clear();
                    disconnect();
                    state = MqttState::Disconnected;
                }
            }

            milliseconds timeout;
            if (powerSaveMode && state != MqttState::Disconnected && keepAliveUntil > now) {
                auto keepAlivePeriod = duration_cast<milliseconds>(keepAliveUntil - now);
                LOGTV("mqtt", "Keeping alive for %lld ms",
                    keepAlivePeriod.count());
                timeout = std::min(keepAlivePeriod, MQTT_LOOP_INTERVAL);
            } else {
                // LOGTV("mqtt", "Waiting for event for %lld ms",
                //     duration_cast<milliseconds>(MQTT_LOOP_INTERVAL).count());
                timeout = MQTT_LOOP_INTERVAL;
            }

            eventQueue.drainIn(duration_cast<ticks>(timeout), [&](const auto& event) {
                milliseconds extendKeepAlive = milliseconds::zero();
                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, Connected>) {
                            LOGTV("mqtt", "Processing connected event, session present: %d",
                                arg.sessionPresent);
                            state = MqttState::Connected;

                            // This is what we have been waiting for, let's keep alive
                            keepAliveUntil = boot_clock::now() + keepAliveAfterConnection;
                            keepAliveAfterConnection = milliseconds::zero();

                            // Next connection can start with a persistent session
                            startCleanSession = false;
                            if (!arg.sessionPresent) {
                                // Re-subscribe to existing subscriptions
                                // because we got a clean session
                                processSubscriptions(subscriptions);
                            }
                        } else if constexpr (std::is_same_v<T, Disconnected>) {
                            LOGTV("mqtt", "Processing disconnected event");
                            state = MqttState::Disconnected;

                            // If we lost connection while keeping alive, let's make
                            // sure we keep alive for a while after reconnection
                            auto now = boot_clock::now();
                            if (keepAliveUntil > now) {
                                keepAliveAfterConnection = duration_cast<milliseconds>(keepAliveUntil - now);
                            }
                            keepAliveUntil = boot_clock::zero();

                            // Clear pending messages and notify waiting tasks
                            for (auto& message : pendingMessages) {
                                notifyWaitingTask(message.waitingTask, false);
                            }
                            pendingMessages.clear();
                        } else if constexpr (std::is_same_v<T, MessagePublished>) {
                            LOGTV("mqtt", "Processing message published");
                            pendingMessages.remove_if([&](const auto& pendingMessage) {
                                if (pendingMessage.messageId == arg.messageId) {
                                    notifyWaitingTask(pendingMessage.waitingTask, arg.success);
                                    extendKeepAlive = std::max(extendKeepAlive, pendingMessage.extendKeepAlive);
                                    return true;
                                } else {
                                    return false;
                                }
                            });
                        } else if constexpr (std::is_same_v<T, OutgoingMessage>) {
                            LOGTV("mqtt", "Processing outgoing message to %s",
                                arg.topic.c_str());
                            int ret = processOutgoingMessage(arg);
                            if (ret < 0) {
                                LOGTD("mqtt", "Error publishing to '%s': %s",
                                    arg.topic.c_str(), ret == -2 ? "outbox full" : "failure");
                                notifyWaitingTask(arg.waitingTask, false);
                            } else {
                                auto messageId = ret;
                                if (arg.waitingTask != nullptr) {
                                    if (messageId == 0) {
                                        // Notify tasks waiting on QoS 0 messages immediately
                                        notifyWaitingTask(arg.waitingTask, true);
                                        extendKeepAlive = std::max(extendKeepAlive, arg.extendKeepAlive);
                                    } else {
                                        // Record pending task
                                        pendingMessages.emplace_back(messageId, arg.waitingTask, arg.extendKeepAlive);
                                    }
                                }
                            }
                        } else if constexpr (std::is_same_v<T, Subscription>) {
                            LOGTV("mqtt", "Processing subscription");
                            subscriptions.push_back(arg);
                            if (state == MqttState::Connected) {
                                // If we are connected, we need to subscribe immediately.
                                processSubscriptions({ arg });
                            } else {
                                // If we are not connected, we need to rely on the next
                                // clean session to make the subscription.
                                startCleanSession = true;
                            }
                            extendKeepAlive = std::max(extendKeepAlive, MQTT_KEEP_ALIVE_AFTER_OUTGOING);
                        }
                    },
                    event);
                if (extendKeepAlive > milliseconds::zero()) {
                    if (state == MqttState::Connected) {
                        keepAliveUntil = std::max(keepAliveUntil, boot_clock::now() + extendKeepAlive);
                    } else {
                        keepAliveAfterConnection = std::max(keepAliveAfterConnection, extendKeepAlive);
                    }
                }
            });
        }
    }

    void connect(bool startCleanSession) {
        // TODO Do not block on WiFi connection
        if (!wifiConnection.has_value()) {
            LOGTV("mqtt", "Connecting to WiFi...");
            wifiConnection.emplace(wifi);
            LOGTV("mqtt", "Connected to WiFi");
        }

        LOGTD("mqtt", "Starting MQTT client, clean session: %d",
            startCleanSession);
        mqttConfig.session.disable_clean_session = !startCleanSession;
        esp_mqtt_set_config(client, &mqttConfig);
        ESP_ERROR_CHECK(esp_mqtt_client_start(client));
    }

    void disconnect() {
        mqttReady.clear();
        LOGTD("mqtt", "Disconnecting from MQTT server");
        ESP_ERROR_CHECK(esp_mqtt_client_disconnect(client));
        ESP_ERROR_CHECK(esp_mqtt_client_stop(client));
        wifiConnection.reset();
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
                LOGTV("mqtt", "Before connecting to MQTT server");
                break;
            }
            case MQTT_EVENT_CONNECTED: {
                LOGTV("mqtt", "Connected to MQTT server");
                mqttReady.set();
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, Connected { (bool) event->session_present });
                break;
            }
            case MQTT_EVENT_DISCONNECTED: {
                LOGTV("mqtt", "Disconnected from MQTT server");
                mqttReady.clear();
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, Disconnected {});
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
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, MessagePublished { event->msg_id, true });
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
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, MessagePublished { event->msg_id, false });
                break;
            }
            default: {
                LOGTW("mqtt", "Unknown event %d", eventId);
                break;
            }
        }
    }

    void notifyWaitingTask(TaskHandle_t task, bool success) {
        if (task != nullptr) {
            uint32_t status = success ? PUBLISH_SUCCESS : PUBLISH_FAILED;
            xTaskNotify(task, status, eSetValueWithOverwrite);
        }
    }

    static void logErrorIfNonZero(const char* message, int error) {
        if (error != 0) {
            LOGTE("mqtt", " - %s: 0x%x", message, error);
        }
    }

    int processOutgoingMessage(const OutgoingMessage message) {
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
        return ret;
    }

    bool processSubscriptions(const std::list<Subscription>& subscriptions) {
        std::vector<esp_mqtt_topic_t> topics;
        for (auto& subscription : subscriptions) {
            LOGTV("mqtt", "Subscribing to topic '%s' (qos = %d)",
                subscription.topic.c_str(), static_cast<int>(subscription.qos));
            topics.emplace_back(subscription.topic.c_str(), static_cast<int>(subscription.qos));
        }

        int ret = esp_mqtt_client_subscribe_multiple(client, topics.data(), topics.size());
        if (ret < 0) {
            LOGTD("mqtt", "Error subscribing: %s",
                ret == -2 ? "outbox full" : "failure");
            return false;
        }
        return true;
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

    String hostname;
    uint32_t port;
    esp_mqtt_client_config_t mqttConfig;
    esp_mqtt_client_handle_t client;

    Queue<std::variant<Connected, Disconnected, MessagePublished, OutgoingMessage, Subscription>> eventQueue;
    Queue<IncomingMessage> incomingQueue;
    // TODO Use a map instead
    std::list<Subscription> subscriptions;

    static constexpr uint32_t PUBLISH_SUCCESS = 1;
    static constexpr uint32_t PUBLISH_FAILED = 2;

    // TODO Review these values
    static constexpr milliseconds MQTT_CONNECTION_TIMEOUT = 30s;
    static constexpr milliseconds MQTT_LOOP_INTERVAL = 1s;
    static constexpr milliseconds MQTT_DISCONNECTED_CHECK_INTERVAL = 5s;
    static constexpr milliseconds MQTT_QUEUE_TIMEOUT = 1s;
    static constexpr milliseconds MQTT_KEEP_ALIVE_AFTER_OUTGOING = 1500ms;
    static constexpr milliseconds MQTT_MAX_TIMEOUT_POWER_SAVE = 1h;

    friend class MqttRoot;
};

}    // namespace farmhub::kernel::mqtt
