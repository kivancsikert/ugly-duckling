#pragma once

#include <chrono>
#include <list>
#include <memory>

#include <ArduinoJson.h>

#include <Task.hpp>
#include <Telemetry.hpp>
#include <drivers/MotorDriver.hpp>

#include <peripherals/Motors.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/valve/Valve.hpp>
#include <peripherals/valve/ValveConfig.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::valve {

inline PeripheralFactory makeFactory(
	const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors,
	ValveControlStrategyType defaultStrategy) {

	return makePeripheralFactory<Valve, ValveSettings, ValveConfig>(
		"valve",
		"valve",
		[motors](PeripheralInitParameters& params, const std::shared_ptr<ValveSettings>& settings) {
			auto motor = findMotor(motors, settings->motor.get());
			auto strategy = settings->createValveControlStrategy(motor);
			auto valve = std::make_shared<Valve>(
				params.name,
				std::move(strategy),
				params.mqttRoot,
				params.services.telemetryPublisher);

			params.registerFeature("valve", [valve](JsonObject& telemetry) {
				valve->populateTelemetry(telemetry);
			});

			return valve;
		},
		defaultStrategy);
}

}    // namespace farmhub::peripherals::valve
