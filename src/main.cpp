#include <atomic>
#include <chrono>

#include <devices/Device.hpp>

extern "C" void app_main() {
    initArduino();

    new farmhub::devices::Device();

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
