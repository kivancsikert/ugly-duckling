#pragma once

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <utility>
#include <variant>

#include <ArduinoJson.h>

#include <Named.hpp>
#include <NvsStore.hpp>
#include <Task.hpp>
#include <Time.hpp>
#include <drivers/MotorDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/api/IValve.hpp>
#include <peripherals/valve/ValveControlStrategy.hpp>

using namespace std::chrono;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::api;

namespace farmhub::peripherals::valve {

class Valve final
    : public api::IValve,
      public Peripheral,
      public HasShutdown {
public:
    Valve(
        const std::string& name,
        std::unique_ptr<ValveControlStrategy> _strategy)
        : Peripheral(name)
        , nvs(name)
        , strategy(std::move(_strategy)) {

        LOGI("Creating valve '%s' with strategy %s",
            name.c_str(), strategy->describe().c_str());

        TargetState initState;
        switch (strategy->getDefaultState()) {
            case TargetState::Open:
                LOGI("Assuming valve '%s' is open by default",
                    name.c_str());
                initState = TargetState::Open;
                break;
            case TargetState::Closed:
                LOGI("Assuming valve '%s' is closed by default",
                    name.c_str());
                initState = TargetState::Closed;
                break;
            default:
                // Try to load from NVS
                TargetState lastStoredState;
                if (nvs.get("state", lastStoredState)) {
                    initState = lastStoredState;
                    LOGI("Restored state for valve '%s' from NVS: %d",
                        name.c_str(), static_cast<int>(state));
                } else {
                    initState = TargetState::Closed;
                    LOGI("No stored state for valve '%s', defaulting to closed",
                        name.c_str());
                }
                break;
        }
        doTransitionTo(initState);
    }

    void populateTelemetry(JsonObject& telemetry) {
        telemetry["state"] = this->state;
    }

    void closeBeforeShutdown() {
        // TODO Lock the valve to prevent concurrent access
        LOGI("Shutting down valve '%s', closing it",
            name.c_str());
        close();
    }

    // Allow graceful shutdown
    void shutdown(const ShutdownParameters& /*params*/) override {
        closeBeforeShutdown();
    }

    bool transitionTo(std::optional<TargetState> target) override {
        return transitionTo(target.value_or(strategy->getDefaultState()));
    }

    ValveState getState() const override {
        return state;
    }

private:
    void open() {
        LOGI("Opening valve '%s'", name.c_str());
        {
            PowerManagementLockGuard sleepLock(PowerManager::noLightSleep);
            strategy->open();
        }
        setState(ValveState::Open);
    }

    void close() {
        LOGI("Closing valve '%s'", name.c_str());
        {
            PowerManagementLockGuard sleepLock(PowerManager::noLightSleep);
            strategy->close();
        }
        setState(ValveState::Closed);
    }

    bool transitionTo(TargetState target) {
        // Ignore if the state is already set
        if ((this->state == ValveState::Open && target == TargetState::Open)
            || (this->state == ValveState::Closed && target == TargetState::Closed)) {
            return false;
        }
        doTransitionTo(target);
        return true;
    }

    void doTransitionTo(TargetState state) {
        switch (state) {
            case TargetState::Open:
                open();
                break;
            case TargetState::Closed:
                close();
                break;
            default:
                // Ignore
                break;
        }
    }

    void setState(ValveState state) {
        this->state = state;
        if (!nvs.set("state", state)) {
            LOGE("Failed to store state for valve '%s': %d",
                name.c_str(), static_cast<int>(state));
        }
    }

    NvsStore nvs;
    const std::unique_ptr<ValveControlStrategy> strategy;
    ValveState state = ValveState::None;
};

}    // namespace farmhub::peripherals::valve
