#include "I2CManager.hpp"
#include "mqtt/MqttRoot.hpp"
#include "drivers/SwitchManager.hpp"
#include "Telemetry.hpp"
#include "devices/DeviceDefinition.hpp"
#include "devices/DeviceConfiguration.hpp"
#include "NvsStore.hpp"
#include "ShutdownManager.hpp"
#include "PulseCounter.hpp"
#include "PwmManager.hpp"
#include "peripherals/Peripheral.hpp"
#include "functions/Function.hpp"
#include "functions/FunctionConfigTracker.hpp"
#include "BootMessage.hpp"
#include "ArduinoJson/Document/JsonDocument.hpp"
#include "ArduinoJson/Array/JsonArray.hpp"
#include <DeviceInit.hpp>

#include <Log.hpp>
#include <memory>
#include <string>
#include <utility>

using namespace cornucopia::ugly_duckling::devices;
using namespace cornucopia::ugly_duckling::functions;
using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::peripherals;

DeviceRuntimeInit initDeviceRuntime(
    const std::shared_ptr<I2CManager>& i2c,
    const std::shared_ptr<MqttRoot>& mqttRoot,
    const std::shared_ptr<SwitchManager>& switches,
    const std::shared_ptr<TelemetryPublisher>& telemetryPublisher,
    const std::shared_ptr<DeviceDefinition>& deviceDefinition,
    const std::shared_ptr<DeviceConfiguration>& deviceConfig,
    const std::shared_ptr<NvsStore>& deviceConfigNvs,
    const std::shared_ptr<ShutdownManager>& shutdownManager,
    const std::string& confirmedFingerprint,
    const std::string& confirmedRequestedAt) {

    auto peripheralsNvs = std::make_shared<NvsStore>("perf-state");
    auto pulseCounterManager = std::make_shared<PulseCounterManager>();
    auto pwm = std::make_shared<PwmManager>();
    auto telemetryCollector = std::make_shared<TelemetryCollector>();

    // Init peripherals
    auto peripheralServices = PeripheralServices {
        .i2c = i2c,
        .mqttDeviceRoot = mqttRoot,
        .nvs = peripheralsNvs,
        .pulseCounterManager = pulseCounterManager,
        .pwmManager = pwm,
        .switches = switches,
        .telemetryPublisher = telemetryPublisher,
    };
    auto peripheralManager = std::make_shared<PeripheralManager>(telemetryCollector, peripheralServices);
    shutdownManager->registerShutdownListener([peripheralManager]() {
        peripheralManager->shutdown();
    });
    deviceDefinition->registerPeripheralFactories(peripheralManager, peripheralServices, deviceConfig);

    // Init functions
    auto functionServices = FunctionServices {
        .peripherals = peripheralManager,
        .telemetryPublisher = telemetryPublisher,
    };
    // Function configuration lives in the same namespace as the device document (deviceConfigNvs,
    // "config-<slot>") -- there is no separate function-cfg namespace any more (docs/Configuration.md,
    // "Storage: envelopes and slots"). Null only when there's no confirmed slot at all, in which case
    // deviceConfig->functions is empty too, so no function is ever created against it.
    auto functionRegistry = std::make_shared<FunctionRegistry>(deviceConfigNvs, functionServices);
    shutdownManager->registerShutdownListener([functionRegistry]() {
        functionRegistry->shutdown();
    });
    deviceDefinition->registerFunctionFactories(functionRegistry);
    FunctionManifestEntry deviceManifestEntry {
        .fingerprint = confirmedFingerprint,
        .requestedAt = confirmedRequestedAt,
    };

    InitState initState = InitState::Success;

    // Init peripherals
    JsonDocument peripheralsInitDoc;
    auto peripheralsInitJson = peripheralsInitDoc.to<JsonArray>();

    auto builtInPeripheralsSettings = deviceDefinition->getBuiltInPeripherals();
    LOGD("Loading configuration for %d built-in peripherals",
        builtInPeripheralsSettings.size());
    for (auto& builtInPeripheralSettings : builtInPeripheralsSettings) {
        if (!peripheralManager->createPeripheral(builtInPeripheralSettings, peripheralsInitJson)) {
            initState = InitState::PeripheralError;
        }
    }

    const auto& peripheralsSettings = deviceConfig->peripherals.get();
    LOGI("Loading configuration for %d user-configured peripherals",
        peripheralsSettings.size());
    for (const auto& peripheralSettings : peripheralsSettings) {
        if (!peripheralManager->createPeripheral(peripheralSettings.get(), peripheralsInitJson)) {
            initState = InitState::PeripheralError;
        }
    }

    // Start ULP pulse counter after all channels have been registered by peripherals above.
    pulseCounterManager->start();

    JsonDocument functionsInitDoc;
    auto functionsInitJson = functionsInitDoc.to<JsonArray>();
    const auto& functionsSettings = deviceConfig->functions.get();
    LOGI("Loading configuration for %d user-configured functions",
        functionsSettings.size());
    for (const auto& functionSettings : functionsSettings) {
        if (!functionRegistry->createFunction(functionSettings.get(), functionsInitJson)) {
            initState = InitState::FunctionError;
        }
    }

    return {
        .functionRegistry = functionRegistry,
        .telemetryCollector = telemetryCollector,
        .deviceManifestEntry = deviceManifestEntry,
        .initState = initState,
        .peripheralsInitDoc = std::move(peripheralsInitDoc),
        .functionsInitDoc = std::move(functionsInitDoc),
    };
}
