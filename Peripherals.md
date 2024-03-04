# Peripherals

* A **device** is a physical object that you can move around that has an MCU and can connect to the hub.

    An example of a _device_ is an actual Ugly Duckling PCB, placed in the field, connected to whatever externals.

    The device publishes telemetry, and receives commands under `/devices/ugly-duckling/$INSTANCE`.

* A **peripheral** is an independently meaningful functionality made available by the device. This can mean to measure and provide telemetry, and/or to be operated via direct commands and/or via published configuration.

    An example of a _peripheral_ is a flow-control device that consists of a valve, a flow sensor, and optionally a thermometer.

    A peripheral publishes telemetry and receives commands and configuration under `/peripherals/$TYPE/$NAME`.

* A **component** is a functional aspect of a peripheral connected to services. Components are created as part of the creation of a peripheral, and are not addressable.

    An example of a _component_ is a valve that depends on a motor driver for operation.

    A component can register its own commands under its owner peripheral’s topic.

    A component will contribute to the telemetry published by its owner peripheral.

    A component will receive the configuration of its owning peripheral; it needs to pick out the data it needs from it.

* A **service** is an internally addressable feature of the device, i.e. that it has a LED connected to a pin, or a motor driver on a certain set of pins, or even its raw pins themselves.
