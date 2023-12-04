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

void connectToWiFiTask(void* parameters) {
    // Explicitly set mode, ESP defaults to STA+AP
    WiFi.mode(WIFI_STA);

    wifiManager.autoConnect("AutoConnectAP");

    // TODO Use time server from MDNS
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    vTaskDelete(NULL);
}

void ensureTimeSyncTask(void* parameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true) {
        time_t now;
        time(&now);
        Serial.printf("Time: %d\n", now);
        if (now > (2022 - 1970) * 365 * 24 * 60 * 60) {
            Serial.println("Time configured, exiting task");
            break;
        }
        Serial.println("No time yet");
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up...");

    // TODO What's a good stack size here?
    xTaskCreate(connectToWiFiTask, "Connect to WiFi", 10000, NULL, 1, NULL);
    xTaskCreate(ensureTimeSyncTask, "Ensure time sync", 10000, NULL, 1, NULL);

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

String wifiStatus() {
    switch (WiFi.status()) {
        case WL_NO_SHIELD:        return "NO SHIELD";
        case WL_IDLE_STATUS:      return "IDLE STATUS";
        case WL_NO_SSID_AVAIL:    return "NO SSID AVAIL";
        case WL_SCAN_COMPLETED:   return "SCAN COMPLETED";
        case WL_CONNECTED:        return "CONNECTED";
        case WL_CONNECT_FAILED:   return "CONNECT FAILED";
        case WL_CONNECTION_LOST:  return "CONNECTION LOST";
        case WL_DISCONNECTED:     return "DISCONNECTED";
        default:                  return "UNKNOWN";
    }
}

void loop() {
    Serial.print("\033[1G\033[0K");
    Serial.print("Uptime: \033[33m" + String(millis()) + "\033[0m ms");
    Serial.print(", wifi: \033[33m" + wifiStatus() + "\033[0m");

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    Serial.printf(", now: \033[33m%d\033[0m", now);
    Serial.print(&timeinfo, ", local time: \033[33m%A, %B %d %Y %H:%M:%S\033[0m");

    delay(100);
}
