idf_component_register(
    SRCS
        drivers/MotorDriver.cpp
    INCLUDE_DIRS "."
    REQUIRES
        # Core ESP-IDF components
        esp_adc
        esp_app_format
        esp_driver_gpio
        esp_driver_i2c
        esp_event
        esp_http_client
        esp_https_ota
        esp_netif
        esp_timer
        esp_wifi
        espcoredump
        espressif__mdns
        mbedtls
        mqtt
        nvs_flash
        spiffs
        wifi_provisioning

        # ESP-IDF-lib components
        bh1750
        ds18x20
        i2cdev
        ina219
        sht3x
        si7021
        tsl2591

        # ArduinoJSON
        bblanchon__arduinojson
        kode_bq27220
)
