#pragma once

#include <chrono>
#include <memory>
#include <utility>

#include <Configuration.hpp>
#include <Named.hpp>
#include <functions/Function.hpp>
#include <functions/ScheduledTransitionLoop.hpp>

#include <peripherals/api/IDoor.hpp>
#include <peripherals/api/ILightSensor.hpp>
#include <utils/scheduling/CompositeScheduler.hpp>
#include <utils/scheduling/OverrideScheduler.hpp>

using namespace farmhub::peripherals::api;
using namespace farmhub::utils::scheduling;

namespace farmhub::functions::chicken_door {

LOGGING_TAG(CHICKEN_DOOR, "chicken-door")

class ChickenDoorConfig : public ConfigurationSection {
public:
    /**
     * @brief Light level above which the door should be open.
     */
    Property<Lux> openLevel { this, "openLevel", 250 };

    /**
     * @brief Light level below which the door should be closed.
     */
    Property<Lux> closeLevel { this, "closeLevel", 10 };

    /**
     * @brief The state to override the schedule with.
     */
    Property<TargetState> overrideState { this, "overrideState" };

    /**
     * @brief Until when the override state is valid.
     */
    Property<time_point<system_clock>> overrideUntil { this, "overrideUntil" };
};

struct LightSensorSchedule {
    Lux openLevel;
    Lux closeLevel;
};

struct LightSensorScheduler : IScheduler {
    LightSensorScheduler(const std::shared_ptr<ILightSensor>& lightSensor)
        : lightSensor(lightSensor) {
    }

    void setTarget(std::optional<LightSensorSchedule> target) {
        this->target = target;
    }

    const char* getName() const override {
        return "light";
    }

    ScheduleResult tick() override {
        if (!target) {
            return {
                .targetState = {},
                .nextDeadline = {},
                .shouldPublishTelemetry = false,
            };
        }
        const auto& target = *this->target;

        auto targetState = calculateTargetState(lightSensor->getLightLevel(), target);
        return {
            .targetState = targetState,
            .nextDeadline = 1min,
            .shouldPublishTelemetry = false,
        };
    }

private:
    static std::optional<TargetState> calculateTargetState(Lux lightLevel, const LightSensorSchedule& target) {
        if (lightLevel >= target.openLevel) {
            return TargetState::Open;
        }
        if (lightLevel <= target.closeLevel) {
            return TargetState::Closed;
        }
        return {};
    }

    std::shared_ptr<ILightSensor> lightSensor;
    std::optional<LightSensorSchedule> target;
};

class ChickenDoor final
    : public Named,
      public HasConfig<ChickenDoorConfig> {
public:
    ChickenDoor(
        const std::string& name,
        const std::shared_ptr<IDoor>& door,
        const std::shared_ptr<OverrideScheduler>& overrideScheduler,
        const std::shared_ptr<LightSensorScheduler>& lightSensorScheduler,
        const std::shared_ptr<TelemetryPublisher>& telemetryPublisher)
        : Named(name) {
        LOGTI(CHICKEN_DOOR, "Initializing chicken-door '%s' with door '%s'",
            name.c_str(),
            door->getName().c_str());

        auto compositeScheduler = std::make_shared<CompositeScheduler>(std::list<std::shared_ptr<IScheduler>> {
            overrideScheduler,
            lightSensorScheduler,
        });

        runScheduledTransitionLoop<IDoor, ConfigSpec>(
            name,
            CHICKEN_DOOR,
            door,
            compositeScheduler,
            telemetryPublisher,
            configQueue,
            [overrideScheduler, lightSensorScheduler](const ConfigSpec& config) {
                overrideScheduler->setOverride(config.overrideSpec);
                lightSensorScheduler->setTarget(config.lightTarget);
            });
    }

    void configure(const std::shared_ptr<ChickenDoorConfig>& config) override {
        auto overrideState = config->overrideState.getIfPresent();
        auto overrideSpec = overrideState.has_value()
            ? std::make_optional<OverrideSchedule>({
                  .state = *overrideState,
                  .until = config->overrideUntil.get(),
              })
            : std::nullopt;
        configQueue.put(ConfigSpec {
            .overrideSpec = overrideSpec,
            .lightTarget = {
                .openLevel = config->openLevel.get(),
                .closeLevel = config->closeLevel.get(),
            },
        });
    }

private:
    struct ConfigSpec {
        std::optional<OverrideSchedule> overrideSpec;
        LightSensorSchedule lightTarget;
    };
    Queue<ConfigSpec> configQueue { "configQueue", 1 };
};

struct ChickenDoorSettings : ConfigurationSection {
    Property<std::string> door { this, "door" };
    Property<std::string> lightSensor { this, "lightSensor" };
};

struct NoOpLightSensor : virtual ILightSensor, Named {
    NoOpLightSensor(const std::string& name)
        : Named(name) {
    }

    Lux getLightLevel() override {
        return -999;
    }

    const std::string& getName() const override {
        return Named::name;
    }
};

inline FunctionFactory makeFactory() {
    return makeFunctionFactory<ChickenDoor, ChickenDoorSettings, ChickenDoorConfig>(
        "chicken-door",
        [](const FunctionInitParameters& params, const std::shared_ptr<ChickenDoorSettings>& settings) {
            auto door = params.peripheral<IDoor>(settings->door.get());
            auto lightSensor = settings->lightSensor.hasValue()
                ? params.peripheral<ILightSensor>(settings->lightSensor.get())
                : std::make_shared<NoOpLightSensor>(params.name + ":light");
            return std::make_shared<ChickenDoor>(
                params.name,
                door,
                std::make_shared<OverrideScheduler>(),
                std::make_shared<LightSensorScheduler>(lightSensor),
                params.services.telemetryPublisher);
        });
}

}    // namespace farmhub::functions::chicken_door
