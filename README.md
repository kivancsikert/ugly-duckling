# Ugly Duckling

![Build badge](https://github.com/kivancsikert/ugly-duckling/actions/workflows/build.yml/badge.svg)

Ugly Duckling is a firmware for IoT devices participating in the FarmHub ecosystem.

The devices are built around the Espressif ESP32 micro-controller using FreeRTOS and the Arduino framework.
The devices can report telemetry to, and receive configuration and commands from the FarmHub server via MQTT over WiFi.
They can also receive firmware updates via HTTP(S).

Devices are identified by a location (denoting teh installation site) and a unique instance name.
Devices can have multiple peripherals, such as sensors, actuators, and displays.
Each peripheral is identified by its type and a name that is unique within the device.

## MQTT configuration

The MQTT configuration is stored in `mqtt-config.json` in the root of the SPIFFS file system:

```jsonc
{
  "host": "...", // broker host name, look up via mDNS if omitted
  "port": 1883, // broker port, defaults to 1883
  "clientId": "chicken-door", // client ID, defaults to "ugly-duckling-$instance" if omitted
  "queueSize": 16 // MQTT message queue size, defaults to 16
}
```

Ugly Duckling supports TLS-encrypted MQTT connections using client-side certificates.
To enable this, the following parameters must be present in the `mqtt-config.json` file:

```jsonc
{
  // ...
  "serverCert": [
    "-----BEGIN CERTIFICATE-----",
    "...",
    "-----END CERTIFICATE-----"
  ],
  "clientCert": [
    "-----BEGIN CERTIFICATE-----",
    "...",
    "-----END CERTIFICATE-----"
  ],
  "clientKey": [
    "-----BEGIN RSA PRIVATE KEY-----",
    "...",
    "-----END RSA PRIVATE KEY-----"
  ]
}
```

The certificates and keys must be in Base64 encoded PEM format, each line must be a separate element in an array.

### MQTT zeroconf

If the `mqtt-config.json` file is missing, or the `mqtt.host` parameter is omitted or left empty, the firmware will try to look up the first MQTT server (host and port) via mDNS/Bonjour.
If there are multiple hits, the first one is used.

## Device configuration

Configuration about the hardware itself is stored in `device-config.json` in the root of the SPIFFS file system.
This describes the device and its peripherals.

```jsonc
{
    "instance": "chicken-door", // the instance name, mandatory
    "location": "my-farm", // the name of the location the device is installed at, mandatory
    "ntp": {
        "host": "pool.ntp.org", // NTP server host name, optional
    },
    "sleepWhenIdle": true, // whether the device should sleep when idle, defaults to false
    "peripherals": [
      {
        "type": "chicken-door",
        "name": "main-coop-door",
        "params": {
          "motor": "b",
          "openPin": "B2",
          "closedPin": "B1",
          "lightSensor": {
            "scl": "C2",
            "sda": "C3"
          }
        }
      }
    ]
}
```

Devices communicate using the topic `/devices/ugly-duckling/$DEVICE_INSTANCE`, or `$DEVICE_ROOT` for short.
For example, during boot, the device will publish a message to `/devices/ugly-duckling/$DEVICE_INSTANCE/init`, or `$DEVICE_ROOT/init` for short.

Peripherals communicate using the topic `$DEVICE_ROOT/peripheral/$PERIPHERAL_NAME`, or `$PERIPHERAL_ROOT` for short.

## Peripheral configuration

Some peripherals can receive custom configurations, for example, a flow controller can have a custom schedule.
These are communicated via MQTT under the `$PERIPHERAL_NAME/config` topic.
Once the device receives such configuration, it stores it under `/p/$PERIPHERAL_NAME.json` in the SPIFFS file system.

## Remote commands

FarmHub devices and their peripherals both support receiving commands via MQTT.
Commands are triggered via retained MQTT messages sent to `$DEVICE_ROOT/commands/$COMMAND` for devices, and `$DEVICE_`.
They typically respond under `$DEVICE_ROOT/responses/$COMMAND`.

Once the device receives a command it deletes the retained message.
This allows commands to be sent to sleeping devices.

There are a few commands supported out-of-the-box:

### Echo

Whatever JSON you send to `$DEVICE_ROOT/commands/echo`.

See `EchoCommand` for more information.

### Restart

Sending a message to `$DEVICE_ROOT/commands/restart` restarts the device immediately.

See `RestartCommand` for more information.

### Firmware update via HTTP

Sending a message to `$DEVICE_ROOT/commands/update` with a URL to a firmware binary (`firmware.bin`), it will instruct the device to update its firmware:

```jsonc
{
    "url": "https://github.com/.../.../releases/download/.../firmware.bin"
}
```

See `HttpUpdateCommand` for more information.

### File commands

The following commands are available to manipulate files on the device via SPIFFS:

- `commands/files/list` returns a list of the files
- `commands/files/read` reads a file at the given `path`
- `commands/files/write` writes the given `contents` to a file at the given `path`
- `commands/files/remove` removes the file at the given `path`

See `FileCommands` for more information.

## Development

### Prerequisites

- ESP-IDF v4.4.8

### Building

There are two ways to build the firmware:

1. Using the ESP-IDF build system. In this case you have to set the right target and pass `UD_GEN` to the build system manually.
2. Use the `idfx.py` wrapper, in which case you have to set `UD_GEN` as an environment variable, and the wrapper will set the right target for you.

You can also set `UD_DEBUG` as an environment variable or add `-DUD_DEBUG=1` to the build command to enable debug output.

```bash
python idfx.py build
```

### Flashing

```bash
python idfx.py flash
```

If you also want to upload the SPIFFS image, add `-DFSUPLOAD=1` to the command:

```bash
python idfx.py -DFSUPLOAD=1 flash
```

### Monitoring

```bash
python idfx.py monitor
```

### Testing

TBD
