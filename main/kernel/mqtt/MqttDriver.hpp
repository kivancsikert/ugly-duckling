#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <optional>
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

typedef std::function<void(const std::string&, const JsonObject&)> SubscriptionHandler;

class MqttRoot;

class MqttDriver {
public:
    class Config : public ConfigurationSection {
    public:
        Property<std::string> host { this, "host", "" };
        Property<unsigned int> port { this, "port", 1883 };
        Property<std::string> clientId { this, "clientId", "" };
        Property<size_t> queueSize { this, "queueSize", 128 };
        ArrayProperty<std::string> serverCert { this, "serverCert" };
        ArrayProperty<std::string> clientCert { this, "clientCert" };
        ArrayProperty<std::string> clientKey { this, "clientKey" };
    };

    MqttDriver(
        WiFiDriver& wifi,
        MdnsDriver& mdns,
        const Config& config,
        const std::string& instanceName,
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
        if (configHostname.empty()) {
#ifdef WOKWI
            hostname = "host.wokwi.internal";
            port = 1883;
#else
            MdnsRecord mqttServer;
            while (!mdns.lookupService("mqtt", "tcp", mqttServer, trustMdnsCache)) {
                LOGTE(Tag::MQTT, "Failed to lookup MQTT server from mDNS");
                trustMdnsCache = false;
                Task::delay(5s);
            }
            trustMdnsCache = true;
            hostname = mqttServer.ipOrHost();
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

        LOGTD(Tag::MQTT, "server: %s:%ld, client ID is '%s'",
            config.broker.address.hostname,
            config.broker.address.port,
            config.credentials.client_id);

        if (!configServerCert.empty()) {
            config.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
            config.broker.verification.certificate = configServerCert.c_str();
            LOGTV(Tag::MQTT, "Server cert:\n%s",
                config.broker.verification.certificate);

            if (!configClientCert.empty() && !configClientKey.empty()) {
                config.credentials.authentication = {
                    .certificate = configClientCert.c_str(),
                    .key = configClientKey.c_str(),
                };
                LOGTV(Tag::MQTT, "Client cert:\n%s",
                    config.credentials.authentication.certificate);
            }
        } else {
            config.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
        }
    }

    shared_ptr<MqttRoot> forRoot(const std::string& topic) {
        return make_shared<MqttRoot>(*this, topic);
    }

    // TODO Review these values
    static constexpr milliseconds MQTT_CONNECTION_TIMEOUT = 30s;
    static constexpr milliseconds MQTT_DEFAULT_PUBLISH_TIMEOUT = 5s;
    static constexpr milliseconds MQTT_SUBSCRIPTION_TIMEOUT = MQTT_DEFAULT_PUBLISH_TIMEOUT;
    static constexpr milliseconds MQTT_MINIMUM_CONNECTED_TIME = 1500ms;
    static constexpr milliseconds MQTT_LOOP_INTERVAL = 1s;
    static constexpr milliseconds MQTT_QUEUE_TIMEOUT = 1s;

private:
    struct PendingMessage {
        const int messageId;
        const TaskHandle_t waitingTask;
        const milliseconds extendKeepAlive;
    };

    struct PendingSubscription {
        const int messageId;
        const time_point<boot_clock> subscribedAt;
        const milliseconds extendKeepAlive;
    };

    struct OutgoingMessage {
        const std::string topic;
        const std::string payload;
        const Retention retain;
        const QoS qos;
        const TaskHandle_t waitingTask;
        const LogPublish log;
        const milliseconds extendKeepAlive;
    };

    struct IncomingMessage {
        const std::string topic;
        const std::string payload;
    };

    struct Subscription {
        const std::string topic;
        const QoS qos;
        const SubscriptionHandler handle;
        const milliseconds extendKeepAlive;
    };

    struct MessagePublished {
        const int messageId;
        const bool success;
    };

    struct Subscribed {
        const int messageId;
    };

    struct Connected {
        const bool sessionPresent;
    };

    struct Disconnected { };

    PublishStatus publish(const std::string& topic, const JsonDocument& json, Retention retain, QoS qos, ticks timeout = MQTT_DEFAULT_PUBLISH_TIMEOUT, milliseconds extendAlertBy = milliseconds::zero(), LogPublish log = LogPublish::Log) {
        if (log == LogPublish::Log) {
#ifdef DUMP_MQTT
            std::string serializedJson;
            serializeJsonPretty(json, serializedJson);
            LOGTD(Tag::MQTT, "Queuing topic '%s'%s (qos = %d, timeout = %lld ms): %s",
                topic.c_str(),
                (retain == Retention::Retain ? " (retain)" : ""),
                static_cast<int>(qos),
                duration_cast<milliseconds>(timeout).count(),
                serializedJson.c_str());
#else
            LOGTV(Tag::MQTT, "Queuing topic '%s'%s (qos = %d, timeout = %lld ms)",
                topic.c_str(),
                (retain == Retention::Retain ? " (retain)" : ""),
                static_cast<int>(qos),
                duration_cast<milliseconds>(timeout).count());
#endif
        }
        std::string payload;
        serializeJson(json, payload);
        return publishAndWait(topic, payload, retain, qos, timeout, extendAlertBy);
    }

    PublishStatus clear(const std::string& topic, Retention retain, QoS qos, ticks timeout = MQTT_DEFAULT_PUBLISH_TIMEOUT, milliseconds extendAlertBy = milliseconds::zero()) {
        LOGTD(Tag::MQTT, "Clearing topic '%s' (qos = %d, timeout = %lld ms)",
            topic.c_str(),
            static_cast<int>(qos),
            duration_cast<milliseconds>(timeout).count());
        return publishAndWait(topic, "", retain, qos, timeout, extendAlertBy);
    }

    PublishStatus publishAndWait(const std::string& topic, const std::string& payload, Retention retain, QoS qos, ticks timeout, milliseconds extendAlertBy) {
        TaskHandle_t waitingTask = timeout == ticks::zero() ? nullptr : xTaskGetCurrentTaskHandle();

        bool offered = eventQueue.offerIn(
            MQTT_QUEUE_TIMEOUT,
            OutgoingMessage {
                topic,
                payload,
                retain,
                qos,
                waitingTask,
                LogPublish::Log,
                extendAlertBy + (qos == QoS::AtMostOnce ? timeout : ticks::zero()) });

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

    bool subscribe(const std::string& topic, QoS qos, SubscriptionHandler handler, milliseconds extendKeepAlive = MQTT_MINIMUM_CONNECTED_TIME) {
        return eventQueue.offerIn(
            MQTT_QUEUE_TIMEOUT,
            // TODO Add an actual timeout, separate from extending keep-alive
            Subscription {
                topic,
                qos,
                handler,
                extendKeepAlive });
    }

    static std::string joinStrings(std::list<std::string> strings) {
        if (strings.empty()) {
            return "";
        }
        std::string result;
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
        auto nextSessionShouldBeClean = true;

        // List of messages we are waiting on
        std::list<PendingMessage> pendingMessages;
        std::list<PendingSubscription> pendingSubscriptions;

        while (true) {
            auto now = boot_clock::now();

            // Cull pending subscriptions
            // TODO Do this with deleted messages?
            pendingSubscriptions.remove_if([&](const auto& pendingSubscription) {
                if (now - pendingSubscription.subscribedAt > MQTT_SUBSCRIPTION_TIMEOUT) {
                    LOGTE(Tag::MQTT, "Subscription timed out with message id %d", pendingSubscription.messageId);
                    // Force next session to start clean, so we can re-subscribe
                    nextSessionShouldBeClean = true;
                    return true;
                } else {
                    return false;
                }
            });

            bool shouldBeConencted =
                // We stay connected all the time if we are not in power save mode
                !powerSaveMode
                // We stay connected if there are pending messages (timeouts will cause them to be removed)
                || !pendingMessages.empty()
                // We stay connected if there are pending subscriptions (we just culled them above)
                || !pendingSubscriptions.empty()
                // We stay connected if recent subscriptions or published QoS = 0 messages force us to keep alive
                || keepAliveUntil > now
                || keepAliveAfterConnection > milliseconds::zero();

            if (shouldBeConencted) {
                switch (state) {
                    case MqttState::Disconnected:
                        connect(nextSessionShouldBeClean);
                        state = MqttState::Connecting;
                        connectionStarted = now;
                        break;
                    case MqttState::Connecting:
                        if (now - connectionStarted > MQTT_CONNECTION_TIMEOUT) {
                            LOGTE(Tag::MQTT, "Connecting to MQTT server timed out");
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
                LOGTV(Tag::MQTT, "Keeping alive for %lld ms",
                    keepAlivePeriod.count());
                timeout = std::min(keepAlivePeriod, MQTT_LOOP_INTERVAL);
            } else {
                // LOGTV(Tag::MQTT, "Waiting for event for %lld ms",
                //     duration_cast<milliseconds>(MQTT_LOOP_INTERVAL).count());
                timeout = MQTT_LOOP_INTERVAL;
            }

            eventQueue.drainIn(duration_cast<ticks>(timeout), [&](const auto& event) {
                milliseconds extendKeepAlive = milliseconds::zero();
                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, Connected>) {
                            LOGTV(Tag::MQTT, "Processing connected event, session present: %d",
                                arg.sessionPresent);
                            state = MqttState::Connected;

                            // This is what we have been waiting for, let's keep alive
                            keepAliveUntil = boot_clock::now() + keepAliveAfterConnection;
                            keepAliveAfterConnection = milliseconds::zero();

                            // TODO Should make it work with persistent sessions, but apparenlty it doesn't
                            // // Next connection can start with a persistent session
                            // nextSessionShouldBeClean = false;

                            if (!arg.sessionPresent) {
                                // Re-subscribe to existing subscriptions
                                // because we got a clean session
                                extendKeepAlive = processSubscriptions(subscriptions, pendingSubscriptions);
                            }
                        } else if constexpr (std::is_same_v<T, Disconnected>) {
                            LOGTV(Tag::MQTT, "Processing disconnected event");
                            state = MqttState::Disconnected;
                            stopClient();

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

                            // Clear pending subscriptions
                            pendingSubscriptions.clear();
                        } else if constexpr (std::is_same_v<T, MessagePublished>) {
                            LOGTV(Tag::MQTT, "Processing message published: %d", arg.messageId);
                            pendingMessages.remove_if([&](const auto& pendingMessage) {
                                if (pendingMessage.messageId == arg.messageId) {
                                    notifyWaitingTask(pendingMessage.waitingTask, arg.success);
                                    extendKeepAlive = std::max(extendKeepAlive, pendingMessage.extendKeepAlive);
                                    return true;
                                } else {
                                    return false;
                                }
                            });
                        } else if constexpr (std::is_same_v<T, Subscribed>) {
                            LOGTV(Tag::MQTT, "Processing subscribed event: %d", arg.messageId);
                            pendingSubscriptions.remove_if([&](const auto& pendingSubscription) {
                                if (pendingSubscription.messageId == arg.messageId) {
                                    extendKeepAlive = std::max(extendKeepAlive, pendingSubscription.extendKeepAlive);
                                    return true;
                                } else {
                                    return false;
                                }
                            });
                        } else if constexpr (std::is_same_v<T, OutgoingMessage>) {
                            LOGTV(Tag::MQTT, "Processing outgoing message to %s",
                                arg.topic.c_str());
                            extendKeepAlive = processOutgoingMessage(arg, pendingMessages);
                        } else if constexpr (std::is_same_v<T, Subscription>) {
                            LOGTV(Tag::MQTT, "Processing subscription");
                            subscriptions.push_back(arg);
                            if (state == MqttState::Connected) {
                                // If we are connected, we need to subscribe immediately.
                                extendKeepAlive = processSubscriptions({ arg }, pendingSubscriptions);
                            } else {
                                // If we are not connected, we need to rely on the next
                                // clean session to make the subscription.
                                nextSessionShouldBeClean = true;
                            }
                        }
                    },
                    event);

                if (extendKeepAlive > milliseconds::zero()) {
                    if (state == MqttState::Connected) {
                        LOGTV(Tag::MQTT, "Extending keep alive while connected by %lld ms",
                            extendKeepAlive.count());
                        keepAliveUntil = std::max(keepAliveUntil, boot_clock::now() + extendKeepAlive);
                    } else {
                        LOGTV(Tag::MQTT, "Extending keep alive after connection by %lld ms",
                            extendKeepAlive.count());
                        keepAliveAfterConnection = std::max(keepAliveAfterConnection, extendKeepAlive);
                    }
                }
            });
        }
    }

    void connect(bool startCleanSession) {
        // TODO Do not block on WiFi connection
        if (!wifiConnection.has_value()) {
            LOGTV(Tag::MQTT, "Connecting to WiFi...");
            wifiConnection.emplace(wifi);
            LOGTV(Tag::MQTT, "Connected to WiFi");
        }

        stopClient();

        mqttConfig.session.disable_clean_session = !startCleanSession;
        esp_mqtt_set_config(client, &mqttConfig);
        LOGTD(Tag::MQTT, "Connecting to %s:%lu, clean session: %d",
            mqttConfig.broker.address.hostname, mqttConfig.broker.address.port, startCleanSession);
        ESP_ERROR_CHECK(esp_mqtt_client_start(client));
        clientRunning = true;
    }

    void disconnect() {
        mqttReady.clear();
        LOGTD(Tag::MQTT, "Disconnecting from MQTT server");
        ESP_ERROR_CHECK(esp_mqtt_client_disconnect(client));
        stopClient();
        wifiConnection.reset();
    }

    void stopClient() {
        if (clientRunning) {
            ESP_ERROR_CHECK(esp_mqtt_client_stop(client));
            clientRunning = false;
        }
    }

    bool clientRunning = false;

    static void handleMqttEventCallback(void* userData, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
        auto event = static_cast<esp_mqtt_event_handle_t>(eventData);
        // LOGTV(Tag::MQTT, "Event dispatched from event loop: base=%s, event_id=%d, client=%p, data=%p, data_len=%d, topic=%p, topic_len=%d, msg_id=%d",
        //     eventBase, event->event_id, event->client, event->data, event->data_len, event->topic, event->topic_len, event->msg_id);
        auto* driver = static_cast<MqttDriver*>(userData);
        driver->handleMqttEvent(eventId, event);
    }

    void handleMqttEvent(int eventId, esp_mqtt_event_handle_t event) {
        switch (eventId) {
            case MQTT_EVENT_BEFORE_CONNECT: {
                LOGTV(Tag::MQTT, "Before connecting to MQTT server");
                break;
            }
            case MQTT_EVENT_CONNECTED: {
                LOGTV(Tag::MQTT, "Connected to MQTT server");
                mqttReady.set();
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, Connected { (bool) event->session_present });
                break;
            }
            case MQTT_EVENT_DISCONNECTED: {
                LOGTV(Tag::MQTT, "Disconnected from MQTT server");
                mqttReady.clear();
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, Disconnected {});
                break;
            }
            case MQTT_EVENT_SUBSCRIBED: {
                LOGTV(Tag::MQTT, "Subscribed, message ID: %d", event->msg_id);
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, Subscribed { event->msg_id });
                break;
            }
            case MQTT_EVENT_UNSUBSCRIBED: {
                LOGTV(Tag::MQTT, "Unsubscribed, message ID: %d", event->msg_id);
                break;
            }
            case MQTT_EVENT_PUBLISHED: {
                LOGTV(Tag::MQTT, "Published, message ID %d", event->msg_id);
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, MessagePublished { event->msg_id, true });
                break;
            }
            case MQTT_EVENT_DATA: {
                std::string topic(event->topic, event->topic_len);
                std::string payload(event->data, event->data_len);
                LOGTV(Tag::MQTT, "Received message on topic '%s'",
                    topic.c_str());
                incomingQueue.offerIn(MQTT_QUEUE_TIMEOUT, IncomingMessage { topic, payload });
                break;
            }
            case MQTT_EVENT_ERROR: {
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                    LOGTE(Tag::MQTT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
                    logErrorIfNonZero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                    logErrorIfNonZero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                    logErrorIfNonZero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
                }
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, MessagePublished { event->msg_id, false });
                break;
            }
            default: {
                LOGTW(Tag::MQTT, "Unknown event %d", eventId);
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
            LOGTE(Tag::MQTT, " - %s: 0x%x", message, error);
        }
    }

    milliseconds processOutgoingMessage(const OutgoingMessage message, std::list<PendingMessage>& pendingMessages) {
        int ret = esp_mqtt_client_enqueue(
            client,
            message.topic.c_str(),
            message.payload.c_str(),
            message.payload.length(),
            static_cast<int>(message.qos),
            message.retain == Retention::Retain,
            true);

        milliseconds extendKeepAliveImmediately = milliseconds::zero();
        if (ret < 0) {
            LOGTD(Tag::MQTT, "Error publishing to '%s': %s",
                message.topic.c_str(), ret == -2 ? "outbox full" : "failure");
            notifyWaitingTask(message.waitingTask, false);
        } else {
            auto messageId = ret;
#ifdef DUMP_MQTT
            if (message.log == LogPublish::Log) {
                LOGTV(Tag::MQTT, "Published to '%s' (size: %d), message ID: %d",
                    message.topic.c_str(), message.payload.length(), messageId);
            }
#endif
            if (message.waitingTask != nullptr) {
                if (messageId == 0) {
                    // Notify tasks waiting on QoS 0 messages immediately
                    notifyWaitingTask(message.waitingTask, true);
                    // ...and then keep alive as much as we need to
                    extendKeepAliveImmediately = message.extendKeepAlive;
                } else {
                    // Record pending task (we'll keep alive after the message has been confirmed)
                    pendingMessages.emplace_back(messageId, message.waitingTask, message.extendKeepAlive);
                }
            }
        }
        return extendKeepAliveImmediately;
    }

    milliseconds processSubscriptions(const std::list<Subscription>& subscriptions, std::list<PendingSubscription>& pendingSubscriptions) {
        milliseconds extendKeepAliveImmediately = milliseconds::zero();
        std::vector<esp_mqtt_topic_t> topics;
        for (auto it = subscriptions.begin(); it != subscriptions.end();) {
            // Break up subscriptions into batches of 8
            milliseconds extendKeepAlive = milliseconds::zero();
            for (; it != subscriptions.end() && topics.size() < 8; it++) {
                const auto& subscription = *it;
                LOGTV(Tag::MQTT, "Subscribing to topic '%s' (qos = %d)",
                    subscription.topic.c_str(), static_cast<int>(subscription.qos));
                topics.emplace_back(subscription.topic.c_str(), static_cast<int>(subscription.qos));
                extendKeepAlive = std::max(extendKeepAlive, subscription.extendKeepAlive);
            }

            extendKeepAliveImmediately = std::max(
                extendKeepAliveImmediately,
                processSubscriptionBatch(topics, extendKeepAlive, pendingSubscriptions));
            topics.clear();
        }
        return extendKeepAliveImmediately;
    }

    milliseconds processSubscriptionBatch(const std::vector<esp_mqtt_topic_t>& topics, milliseconds extendKeepAlive, std::list<PendingSubscription>& pendingSubscriptions) {
        int ret = esp_mqtt_client_subscribe_multiple(client, topics.data(), topics.size());

        milliseconds extendKeepAliveImmediately = milliseconds::zero();
        if (ret < 0) {
            LOGTD(Tag::MQTT, "Error subscribing: %s",
                ret == -2 ? "outbox full" : "failure");
        } else {
            auto messageId = ret;
            LOGTV(Tag::MQTT, "%d subscriptions published, message ID = %d",
                topics.size(), messageId);
            if (messageId == 0) {
                // ...and then keep alive as much as we need to
                extendKeepAliveImmediately = extendKeepAlive;
            } else {
                // Record pending task (we'll keep alive after the message has been confirmed)
                pendingSubscriptions.emplace_back(messageId, boot_clock::now(), extendKeepAlive);
            }
        }
        return extendKeepAliveImmediately;
    }

    void processIncomingMessage(const IncomingMessage& message) {
        const std::string& topic = message.topic;
        const std::string& payload = message.payload;

        if (payload.empty()) {
            LOGTV(Tag::MQTT, "Ignoring empty payload");
            return;
        }

#ifdef DUMP_MQTT
        LOGTD(Tag::MQTT, "Received '%s' (size: %d): %s",
            topic.c_str(), payload.length(), payload.c_str());
#else
        LOGTD(Tag::MQTT, "Received '%s' (size: %d)",
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
                    LOGTE(Tag::MQTT, "Incoming handler for topic '%s' timed out",
                        topic.c_str());
                }
                return;
            }
        }
        LOGTW(Tag::MQTT, "No handler for topic '%s'",
            topic.c_str());
    }

    static std::string getClientId(const std::string& clientId, const std::string& instanceName) {
        if (clientId.length() > 0) {
            return clientId;
        }
        return "ugly-duckling-" + instanceName;
    }

    WiFiDriver& wifi;
    std::optional<WiFiConnection> wifiConnection;
    MdnsDriver& mdns;
    bool trustMdnsCache = true;

    const std::string configHostname;
    const int configPort;
    const std::string configServerCert;
    const std::string configClientCert;
    const std::string configClientKey;
    const std::string clientId;

    const bool powerSaveMode;
    StateSource& mqttReady;

    std::string hostname;
    uint32_t port;
    esp_mqtt_client_config_t mqttConfig {};
    esp_mqtt_client_handle_t client;

    Queue<std::variant<Connected, Disconnected, MessagePublished, Subscribed, OutgoingMessage, Subscription>> eventQueue;
    Queue<IncomingMessage> incomingQueue;
    // TODO Use a map instead
    std::list<Subscription> subscriptions;

    static constexpr uint32_t PUBLISH_SUCCESS = 1;
    static constexpr uint32_t PUBLISH_FAILED = 2;

    friend class MqttRoot;
};

}    // namespace farmhub::kernel::mqtt
