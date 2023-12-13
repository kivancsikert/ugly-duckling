#pragma once

#include <chrono>
#include <functional>
#include <utility>

#include <freertos/FreeRTOS.h>

using namespace std::chrono;

namespace farmhub { namespace kernel {

template <typename TMessage>
class Queue {
public:
    Queue(const String& name, size_t capacity = 16, milliseconds sendTimeout = milliseconds::zero(), milliseconds receiveTimeout = milliseconds::zero())
        : name(name)
        , queue(xQueueCreate(capacity, sizeof(TMessage*)))
        , sendTimeout(pdMS_TO_TICKS(sendTimeout.count()))
        , receiveTimeout(pdMS_TO_TICKS(receiveTimeout.count())) {
    }

    ~Queue() {
        vQueueDelete(queue);
    }

    typedef std::function<void(const TMessage&)> MessageHandler;

    template <typename... Args>
    bool send(Args&&... args) {
        TMessage* copy = new TMessage(std::forward<Args>(args)...);
        bool sentWithoutDropping = xQueueSend(queue, &copy, sendTimeout) == pdTRUE;
        if (!sentWithoutDropping) {
            Serial.println("Overflow in queue '" + name + "', dropping message");
            delete copy;
        }
        return sentWithoutDropping;
    }

    int process(MessageHandler handler) {
        int count = 0;
        while (receiveNext(queue, handler)) {
            count++;
        }
        return count;
    }

    bool receiveNext(MessageHandler handler) {
        TMessage* message;
        if (!xQueueReceive(queue, &message, receiveTimeout)) {
            return false;
        }
        handler(*message);
        delete message;
        return true;
    }

private:
    const String name;
    const QueueHandle_t queue;
    const TickType_t sendTimeout;
    const TickType_t receiveTimeout;
};

}}    // namespace farmhub::kernel
