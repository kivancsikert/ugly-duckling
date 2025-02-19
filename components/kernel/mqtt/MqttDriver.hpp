#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <esp_event.h>
#include <mqtt_client.h>

#include <Concurrent.hpp>
#include <Configuration.hpp>
#include <State.hpp>
#include <Task.hpp>
#include <drivers/MdnsDriver.hpp>

using namespace std::chrono_literals;
using namespace farmhub::kernel;
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
        State& networkReady,
        std::shared_ptr<MdnsDriver> mdns,
        const std::shared_ptr<Config> config,
        const std::string& instanceName,
        StateSource& ready)
        : networkReady(networkReady)
        , mdns(mdns)
        , configHostname(config->host.get())
        , configPort(config->port.get())
        , configServerCert(joinStrings(config->serverCert.get()))
        , configClientCert(joinStrings(config->clientCert.get()))
        , configClientKey(joinStrings(config->clientKey.get()))
        , clientId(getClientId(config->clientId.get(), instanceName))
        , ready(ready)
        , eventQueue("mqtt-outgoing", config->queueSize.get())
        , incomingQueue("mqtt-incoming", config->queueSize.get()) {

        Task::run("mqtt", 5120, [this](Task& task) {
            configMqttClient(mqttConfig);
            client = esp_mqtt_client_init(&mqttConfig);

            ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, handleMqttEventCallback, this));

            runEventLoop(task);
        });
        Task::loop("mqtt:incoming", 4096, [this](Task& task) {
            incomingQueue.take([this](const IncomingMessage& message) {
                processIncomingMessage(message);
            });
        });
    }

    State& getReady() {
        return ready;
    }

    void configMqttClient(esp_mqtt_client_config_t& config) {
        if (configHostname.empty()) {
#ifdef WOKWI
            hostname = "host.wokwi.internal";
            port = 1883;
#else
            MdnsRecord mqttServer;
            while (!mdns->lookupService("mqtt", "tcp", mqttServer, trustMdnsCache)) {
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
            .buffer {
                .size = 2048,
            }
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

    // TODO Review these values
    static constexpr milliseconds MQTT_CONNECTION_TIMEOUT = 30s;
    static constexpr milliseconds MQTT_DEFAULT_PUBLISH_TIMEOUT = 5s;
    static constexpr milliseconds MQTT_SUBSCRIPTION_TIMEOUT = MQTT_DEFAULT_PUBLISH_TIMEOUT;
    static constexpr milliseconds MQTT_MINIMUM_CONNECTED_TIME = 1500ms;
    static constexpr milliseconds MQTT_LOOP_INTERVAL = 1s;
    static constexpr milliseconds MQTT_QUEUE_TIMEOUT = 1s;

private:
    struct PendingSubscription {
        const int messageId;
        const time_point<boot_clock> subscribedAt;
    };

    struct OutgoingMessage {
        const std::string topic;
        const std::string payload;
        const Retention retain;
        const QoS qos;
        const TaskHandle_t waitingTask;
        const LogPublish log;
    };

    struct IncomingMessage {
        const std::string topic;
        const std::string payload;
    };

    struct Subscription {
        const std::string topic;
        const QoS qos;
        const SubscriptionHandler handle;
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

    PublishStatus publish(const std::string& topic, const JsonDocument& json, Retention retain, QoS qos, ticks timeout = MQTT_DEFAULT_PUBLISH_TIMEOUT, LogPublish log = LogPublish::Log) {
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
        return publishAndWait(topic, payload, retain, qos, timeout);
    }

    PublishStatus clear(const std::string& topic, Retention retain, QoS qos, ticks timeout = MQTT_DEFAULT_PUBLISH_TIMEOUT) {
        LOGTD(Tag::MQTT, "Clearing topic '%s' (qos = %d, timeout = %lld ms)",
            topic.c_str(),
            static_cast<int>(qos),
            duration_cast<milliseconds>(timeout).count());
        return publishAndWait(topic, "", retain, qos, timeout);
    }

    PublishStatus publishAndWait(const std::string& topic, const std::string& payload, Retention retain, QoS qos, ticks timeout) {
        TaskHandle_t waitingTask = timeout == ticks::zero() ? nullptr : xTaskGetCurrentTaskHandle();

        bool offered = eventQueue.offerIn(
            MQTT_QUEUE_TIMEOUT,
            OutgoingMessage {
                topic,
                payload,
                retain,
                qos,
                waitingTask,
                LogPublish::Log });

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
                pendingMessages.cancelWaitingOn(waitingTask);
                return PublishStatus::TimeOut;
            case PUBLISH_SUCCESS:
                return PublishStatus::Success;
            case PUBLISH_FAILED:
            default:
                return PublishStatus::Failed;
        }
    }

    bool subscribe(const std::string& topic, QoS qos, SubscriptionHandler handler) {
        return eventQueue.offerIn(
            MQTT_QUEUE_TIMEOUT,
            // TODO Add an actual timeout
            Subscription {
                topic,
                qos,
                handler });
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
    class PendingMessages {
    public:
        bool waitOn(int messageId, TaskHandle_t waitingTask) {
            if (waitingTask == nullptr) {
                // Nothing is waiting
                return false;
            }

            if (messageId == 0) {
                // Notify tasks waiting on QoS 0 messages immediately
                notifyWaitingTask(waitingTask, true);
                return false;
            }

            // Record pending task
            Lock lock(mutex);
            messages[messageId] = waitingTask;
            return true;
        }

        bool handlePublished(int messageId, bool success) {
            if (messageId == 0) {
                return false;
            }

            Lock lock(mutex);
            auto it = messages.find(messageId);
            if (it != messages.end()) {
                notifyWaitingTask(it->second, success);
                messages.erase(it);
                return true;
            }
            return false;
        }

        bool cancelWaitingOn(TaskHandle_t waitingTask) {
            if (waitingTask == nullptr) {
                return false;
            }

            Lock lock(mutex);
            bool removed = false;
            for (auto it = messages.begin(); it != messages.end();) {
                if (it->second == waitingTask) {
                    it = messages.erase(it);
                    removed = true;
                } else {
                    ++it;
                }
            }
            return removed;
        }

        void clear() {
            Lock lock(mutex);
            for (auto& [messageId, waitingTask] : messages) {
                notifyWaitingTask(waitingTask, false);
            }
            messages.clear();
        }

        static void notifyWaitingTask(TaskHandle_t task, bool success) {
            if (task != nullptr) {
                uint32_t status = success ? PUBLISH_SUCCESS : PUBLISH_FAILED;
                xTaskNotify(task, status, eSetValueWithOverwrite);
            }
        }

    private:
        Mutex mutex;
        std::unordered_map<int, TaskHandle_t> messages;
    };

    void runEventLoop(Task& task) {
        // We are not yet connected
        auto state = MqttState::Disconnected;
        auto connectionStarted = boot_clock::zero();

        // The first session is always clean
        auto nextSessionShouldBeClean = true;

        // List of messages we are waiting on
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

            switch (state) {
                case MqttState::Disconnected:
                    connect(nextSessionShouldBeClean);
                    state = MqttState::Connecting;
                    connectionStarted = now;
                    break;
                case MqttState::Connecting:
                    if (now - connectionStarted > MQTT_CONNECTION_TIMEOUT) {
                        LOGTE(Tag::MQTT, "Connecting to MQTT server timed out");
                        ready.clear();
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

            eventQueue.drainIn(duration_cast<ticks>(MQTT_LOOP_INTERVAL), [&](const auto& event) {
                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, Connected>) {
                            LOGTV(Tag::MQTT, "Processing connected event, session present: %d",
                                arg.sessionPresent);
                            state = MqttState::Connected;

                            // TODO Should make it work with persistent sessions, but apparenlty it doesn't
                            // // Next connection can start with a persistent session
                            // nextSessionShouldBeClean = false;

                            if (!arg.sessionPresent) {
                                // Re-subscribe to existing subscriptions
                                // because we got a clean session
                                processSubscriptions(subscriptions, pendingSubscriptions);
                            }
                        } else if constexpr (std::is_same_v<T, Disconnected>) {
                            LOGTV(Tag::MQTT, "Processing disconnected event");
                            state = MqttState::Disconnected;
                            stopClient();

                            // Clear pending messages and notify waiting tasks
                            pendingMessages.clear();

                            // Clear pending subscriptions
                            pendingSubscriptions.clear();
                        } else if constexpr (std::is_same_v<T, MessagePublished>) {
                            LOGTV(Tag::MQTT, "Processing message published: %d", arg.messageId);
                            pendingMessages.handlePublished(arg.messageId, arg.success);
                        } else if constexpr (std::is_same_v<T, Subscribed>) {
                            LOGTV(Tag::MQTT, "Processing subscribed event: %d", arg.messageId);
                            pendingSubscriptions.remove_if([&](const auto& pendingSubscription) {
                                return pendingSubscription.messageId == arg.messageId;
                            });
                        } else if constexpr (std::is_same_v<T, OutgoingMessage>) {
                            LOGTV(Tag::MQTT, "Processing outgoing message to %s",
                                arg.topic.c_str());
                            processOutgoingMessage(arg);
                        } else if constexpr (std::is_same_v<T, Subscription>) {
                            LOGTV(Tag::MQTT, "Processing subscription");
                            subscriptions.push_back(arg);
                            if (state == MqttState::Connected) {
                                // If we are connected, we need to subscribe immediately.
                                processSubscriptions({ arg }, pendingSubscriptions);
                            } else {
                                // If we are not connected, we need to rely on the next
                                // clean session to make the subscription.
                                nextSessionShouldBeClean = true;
                            }
                        }
                    },
                    event);
            });
        }
    }

    void connect(bool startCleanSession) {
        networkReady.awaitSet();

        stopClient();

        mqttConfig.session.disable_clean_session = !startCleanSession;
        esp_mqtt_set_config(client, &mqttConfig);
        LOGTD(Tag::MQTT, "Connecting to %s:%lu, clean session: %d",
            mqttConfig.broker.address.hostname, mqttConfig.broker.address.port, startCleanSession);
        ESP_ERROR_CHECK(esp_mqtt_client_start(client));
        clientRunning = true;
    }

    void disconnect() {
        ready.clear();
        LOGTD(Tag::MQTT, "Disconnecting from MQTT server");
        ESP_ERROR_CHECK(esp_mqtt_client_disconnect(client));
        stopClient();
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
                LOGTD(Tag::MQTT, "Connecting to MQTT server %s:%lu", hostname.c_str(), port);
                break;
            }
            case MQTT_EVENT_CONNECTED: {
                LOGTD(Tag::MQTT, "Connected to MQTT server");
                ready.set();
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, Connected { (bool) event->session_present });
                break;
            }
            case MQTT_EVENT_DISCONNECTED: {
                LOGTD(Tag::MQTT, "Disconnected from MQTT server");
                ready.clear();
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
            case MQTT_EVENT_DELETED: {
                LOGTV(Tag::MQTT, "Deleted, message ID %d", event->msg_id);
                eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, MessagePublished { event->msg_id, false });
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
                switch (event->error_handle->error_type) {
                    case MQTT_ERROR_TYPE_TCP_TRANSPORT:
                        LOGTE(Tag::MQTT, "TCP transport error; transport socket errno: %d, TLS last ESP error: 0x%x, TLS stack error: 0x%x, TLS cert verify flags: 0x%x",
                            event->error_handle->esp_transport_sock_errno,
                            event->error_handle->esp_tls_last_esp_err,
                            event->error_handle->esp_tls_stack_err,
                            event->error_handle->esp_tls_cert_verify_flags);
                        break;

                    case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
                        LOGTE(Tag::MQTT, "Connection refused; return code: %d",
                            event->error_handle->connect_return_code);
                        break;

                    case MQTT_ERROR_TYPE_SUBSCRIBE_FAILED:
                        LOGTE(Tag::MQTT, "Subscribe failed; message ID: %d",
                            event->msg_id);
                        break;

                    case MQTT_ERROR_TYPE_NONE:
                        // Nothing to report
                        break;
                }
                if (event->msg_id != 0) {
                    eventQueue.offerIn(MQTT_QUEUE_TIMEOUT, MessagePublished { event->msg_id, false });
                }
                break;
            }
            default: {
                LOGTW(Tag::MQTT, "Unknown event %d", eventId);
                break;
            }
        }
    }

    void processOutgoingMessage(const OutgoingMessage& message) {
        int ret = esp_mqtt_client_enqueue(
            client,
            message.topic.c_str(),
            message.payload.c_str(),
            message.payload.length(),
            static_cast<int>(message.qos),
            message.retain == Retention::Retain,
            true);

        if (ret < 0) {
            LOGTD(Tag::MQTT, "Error publishing to '%s': %s",
                message.topic.c_str(), ret == -2 ? "outbox full" : "failure");
            PendingMessages::notifyWaitingTask(message.waitingTask, false);
        } else {
            auto messageId = ret;
#ifdef DUMP_MQTT
            if (message.log == LogPublish::Log) {
                LOGTV(Tag::MQTT, "Published to '%s' (size: %d), message ID: %d",
                    message.topic.c_str(), message.payload.length(), messageId);
            }
#endif
            pendingMessages.waitOn(messageId, message.waitingTask);
        }
    }

    void processSubscriptions(const std::list<Subscription>& subscriptions, std::list<PendingSubscription>& pendingSubscriptions) {
        std::vector<esp_mqtt_topic_t> topics;
        for (auto it = subscriptions.begin(); it != subscriptions.end();) {
            // Break up subscriptions into batches
            for (; it != subscriptions.end() && topics.size() < 8; it++) {
                const auto& subscription = *it;
                LOGTV(Tag::MQTT, "Subscribing to topic '%s' (qos = %d)",
                    subscription.topic.c_str(), static_cast<int>(subscription.qos));
                topics.emplace_back(subscription.topic.c_str(), static_cast<int>(subscription.qos));
            }

            processSubscriptionBatch(topics, pendingSubscriptions);
            topics.clear();
        }
    }

    void processSubscriptionBatch(const std::vector<esp_mqtt_topic_t>& topics, std::list<PendingSubscription>& pendingSubscriptions) {
        int ret = esp_mqtt_client_subscribe_multiple(client, topics.data(), topics.size());

        if (ret < 0) {
            LOGTD(Tag::MQTT, "Error subscribing: %s",
                ret == -2 ? "outbox full" : "failure");
        } else {
            auto messageId = ret;
            LOGTV(Tag::MQTT, "%d subscriptions published, message ID = %d",
                topics.size(), messageId);
            if (messageId > 0) {
                // Record pending task
                pendingSubscriptions.emplace_back(messageId, boot_clock::now());
            }
        }
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
                Task::run("mqtt:incoming-handler", 4096, [topic, payload, subscription](Task& task) {
                    JsonDocument json;
                    deserializeJson(json, payload);
                    subscription.handle(topic, json.as<JsonObject>());
                });
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

    State& networkReady;
    const std::shared_ptr<MdnsDriver> mdns;
    bool trustMdnsCache = true;

    const std::string configHostname;
    const int configPort;
    const std::string configServerCert;
    const std::string configClientCert;
    const std::string configClientKey;
    const std::string clientId;

    StateSource& ready;

    std::string hostname;
    uint32_t port;
    esp_mqtt_client_config_t mqttConfig {};
    esp_mqtt_client_handle_t client;

    Queue<std::variant<Connected, Disconnected, MessagePublished, Subscribed, OutgoingMessage, Subscription>> eventQueue;
    Queue<IncomingMessage> incomingQueue;
    // TODO Use a map instead
    std::list<Subscription> subscriptions;
    PendingMessages pendingMessages;

    static constexpr uint32_t PUBLISH_SUCCESS = 1;
    static constexpr uint32_t PUBLISH_FAILED = 2;

    friend class MqttRoot;
};

}    // namespace farmhub::kernel::mqtt
