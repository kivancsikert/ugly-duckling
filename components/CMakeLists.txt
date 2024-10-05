FILE(GLOB_RECURSE adafruit_busio_sources Adafruit_BusIO/*.cpp)
FILE(GLOB_RECURSE mqtt_sources MQTTPubSubClient/src/MQTTPubSubClient/*.*)
idf_component_register(
    SRCS ${adafruit_busio_sources}
    INCLUDE_DIRS "Adafruit_BusIO"

    SRCS "Adafruit_Sensor/Adafruit_Sensor.cpp"
    INCLUDE_DIRS "Adafruit_Sensor"

    SRCS "Adafruit_TSL2591_Library/Adafruit_TSL2591.cpp"
    INCLUDE_DIRS "Adafruit_TSL2591_Library"

    SRCS "BH1750/src/BH1750.cpp"
    INCLUDE_DIRS "BH1750/src"

    SRCS "DallasTemperature/DallasTemperature.cpp"
    INCLUDE_DIRS "DallasTemperature"

    SRCS ${mqtt_sources}
    INCLUDE_DIRS "MQTTPubSubClient/src" "ArxTypeTraits" "ArxContainer"

    SRCS "NTPClient/NTPClient.cpp"
    INCLUDE_DIRS "NTPClient"

    SRCS "OneWire/OneWire.cpp"
    INCLUDE_DIRS "OneWire"

    SRCS "SHT31/SHT31.cpp"
    INCLUDE_DIRS "SHT31"

    SRCS "SHT2x/SHT2x.cpp"
    INCLUDE_DIRS "SHT2x"

    SRCS "WiFiManager/WiFiManager.cpp"
    INCLUDE_DIRS "WiFiManager"

    REQUIRES "arduino-esp32"
)
