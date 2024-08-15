#include <atomic>
#include <chrono>

// Helper macros to convert macro to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <devices/Device.hpp>

extern "C" void app_main() {
    initArduino();

    new farmhub::devices::Device();

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
