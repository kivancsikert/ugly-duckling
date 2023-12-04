#include <Arduino.h>

#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiMulti.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Define the GPIO pin numbers for the LEDs
const int ledPin1 = 2;    // Adjust the pin numbers according to your setup
const int ledPin2 = 4;

// Task functions
void blinkLED1(void* parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (true) {
        digitalWrite(ledPin1, HIGH);                                   // Turn the LED on
        xTaskDelayUntil(&xLastWakeTime, 1500 / portTICK_PERIOD_MS);    // delay for half the period (1500 ms)
        digitalWrite(ledPin1, LOW);                                    // Turn the LED off
        xTaskDelayUntil(&xLastWakeTime, 1500 / portTICK_PERIOD_MS);    // delay for half the period (1500 ms)
    }
}

void blinkLED2(void* parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true) {
        digitalWrite(ledPin2, HIGH);
        xTaskDelayUntil(&xLastWakeTime, 2500 / portTICK_PERIOD_MS);    // delay for half the period (1500 ms)
        digitalWrite(ledPin2, LOW);
        xTaskDelayUntil(&xLastWakeTime, 2500 / portTICK_PERIOD_MS);    // delay for half the period (1500 ms)
    }
}

WiFiManager wifiManager;

void connectToWiFiTask(void* parameter) {
    // Explicitly set mode, ESP defaults to STA+AP
    WiFi.mode(WIFI_STA);

    wifiManager.autoConnect("AutoConnectAP");

    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up...");

    // TODO What's a good stack size here?
    xTaskCreate(connectToWiFiTask, "Connect to WiFi", 10000, NULL, 1, NULL);

    // TODO Use time server from MDNS
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.println("WiFi: connected to " + WiFi.SSID());
    },
        ARDUINO_EVENT_WIFI_STA_CONNECTED);

    // Initialize the LED pin as an output
    pinMode(ledPin1, OUTPUT);
    pinMode(ledPin2, OUTPUT);

    // Now create the tasks
    xTaskCreate(blinkLED1, "LED1 Blink", 1000, NULL, 1, NULL);
    xTaskCreate(blinkLED2, "LED2 Blink", 1000, NULL, 1, NULL);
}

void loop() {
    Serial.print("\033[1G\033[0K");
    Serial.print("Uptime: \033[33m" + String(millis()) + "\033[0m ms");
    Serial.print(", wifi connected: \033[33m" + String(WiFi.isConnected()) + "\033[0m ms");
    delay(100);
}
