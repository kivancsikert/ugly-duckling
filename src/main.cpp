#include <atomic>
#include <chrono>

#include <Arduino.h>

#include <ArduinoLog.h>

#include <devices/Device.hpp>

extern "C" void app_main() {
    initArduino();

    new farmhub::devices::Device();

    Log.infoln("Application initialized, entering idle loop");

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
