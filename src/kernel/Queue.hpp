#pragma once

#include <chrono>
#include <functional>
#include <utility>

#include <freertos/FreeRTOS.h>

#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub { namespace kernel {

template <typename TMessage>
class Queue {
public:
    Queue(const String& name, size_t capacity = 16, ticks sendTimeout = ticks::zero(), ticks receiveTimeout = ticks::zero())
        : name(name)
        , queue(xQueueCreate(capacity, sizeof(TMessage*)))
        , sendTimeout(sendTimeout)
        , receiveTimeout(receiveTimeout) {
    }

    ~Queue() {
        vQueueDelete(queue);
    }

    typedef std::function<void(const TMessage&)> MessageHandler;

    template <typename... Args>
    bool send(Args&&... args) {
        TMessage* copy = new TMessage(std::forward<Args>(args)...);
        bool sentWithoutDropping = xQueueSend(queue, &copy, sendTimeout.count()) == pdTRUE;
        if (!sentWithoutDropping) {
            Serial.println("Overflow in queue '" + name + "', dropping message");
            delete copy;
        }
        return sentWithoutDropping;
    }

    int process(MessageHandler handler) {
        int count = 0;
        while (receiveNext(handler)) {
            count++;
        }
        return count;
    }

    bool receiveNext(MessageHandler handler) {
        TMessage* message;
        if (!xQueueReceive(queue, &message, receiveTimeout.count())) {
            return false;
        }
        handler(*message);
        delete message;
        return true;
    }

private:
    const String name;
    const QueueHandle_t queue;
    const ticks sendTimeout;
    const ticks receiveTimeout;
};

}}    // namespace farmhub::kernel
