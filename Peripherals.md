# Peripherals

* A **device** is a physical object that you can move around that has an MCU and can connect to the hub.

  An example of a _device_ is an actual Ugly Duckling PCB, placed in the field, connected to whatever externals.

  The device publishes telemetry, and receives configuration and commands under `/devices/ugly-duckling/$INSTANCE`.
  This is called the **device topic**, and we'll refer to it as `...` further down.

* A **peripheral** is an external physical element that is connected to the _device._

  An example of a _peripheral_ is a flow-meter sensor, a thermometer, a valve or a motor.

  Peripherals have names that identify them in the context of their owning device. They also have types that define their functionality,
  and the features they expose.

  Peripherals are initialized based on parameters in the **device settings** (`/device-config.json` on the file system).

* Peripherals publish telemetry as **features**

  An SHT3x peripheral for example will publish two numeric features: the `temperature` of the air and the `moisture` percentage,
  A `valve` will publish a sigle `valve` feature that includes the state of the valve and any overrides being active at the time.

  Features are addressed by their type and the name of the peripheral they belong to.

  Telemetry for each features is published as part of the device's telemetry message under `.../telemetry`.
  The features are listed under `features` in the telemetry message with `type` and `name` specified.
  We publish telemetry as a single message per device to reduce the number of topics published.
  Doing so also ensures atomic updates to telemetry.

* Peripherals are grouped into **functions**

  A _function_ is a logical grouping of specific peripherals that constitute a real-world function of the device.

  For example, a device can have a "plot controller" function.
  This requires a valve peripheral to be present, and can also have a flow sensor be attached.
  Furthermore, the plot controller function can utilize a soil moisture and a soil temperature sensor peripheral to improve irrigation accuracy.

  Another example of a function is the "chicken door": it requires a motor peripheral and a light sensor peripheral to be present.
  It can also utilize limit switch peripherals to sense when the door is fully opened or closed.

  Functions are the unit of runtime configuration.
  A plot controller can be configured to open the valve on a schedule, or to ensure the soil moisture level stays between 60-80%.
  Configuration is sent to the device under `.../config`, with each feature having its own configuration object.
