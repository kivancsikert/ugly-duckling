#include <atomic>
#include <chrono>

#include <ArduinoLog.h>

#include <devices/Device.hpp>

extern "C" void app_main() {
    initArduino();

    new farmhub::devices::Device();

    Log.infoln("Device ready in %d ms, entering idle loop",
        millis());

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
