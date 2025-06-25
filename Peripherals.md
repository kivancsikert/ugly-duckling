# Peripherals

* A **device** is a physical object that you can move around that has an MCU and can connect to the hub.

    An example of a _device_ is an actual Ugly Duckling PCB, placed in the field, connected to whatever externals.

    The device publishes telemetry, and receives configuration and commands under `/devices/ugly-duckling/$INSTANCE`.
    This is called the **device topic**, and we'll refer to it as `...` further down.

* A **peripheral** is a unit of functionality that is independently connected to the _device._

    An example of a _peripheral_ is a flow-control device that consists of a valve, a flow sensor, and optionally a thermometer.

    The primary function of peripherals is to configure _features._

    Peripherals are configured via **device settings** (`/device-config.json` on the file system).

    Peripherals have names (defaulting to `default`). These names are only used to derive names for features.

* A **feature** is an addressable, functional aspect of a peripheral. Features provide telemetry and/or can be controlled via commands.

    For example, a flow-control peripheral may have a `valve` feature and a `flow` feature.
    The `valve` feature reports the valve's state, can be configured (a schedule can be set), and can be controlled via commands (i.e. the state can be overridden).
    The `flow` feature reports its flow rate.

    Features are addressed by their types and names.

Telemetry for each features is published as part of the device's telemetry message under `.../telemetry`.
The features are listed under `features` in the telemetry message with `type` and `name` specified.
We publish telemetry as a single message per device to reduce the number of topics published.
Doing so also ensures atomic updates to telemetry.

Configuration for features is stored as retained messages under `.../config`.
Similar to telemetry, the configuration is stored as a single message per device with a `features` array.
Each feature's configuration is specified with `type`, `name`, and the actual configuration data.

Commands are sent to features under the device topic, i.e. `.../commands`; the sender has to specify the `type` and `name` of the feature.
Again, we use a single topic so fewer topics need to be subscribed to and handled.
