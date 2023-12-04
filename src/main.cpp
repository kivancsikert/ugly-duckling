#include <Arduino.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiManager.h>

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

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up...");
}

void loop() {
    // Initialize the LED pin as an output
    pinMode(ledPin1, OUTPUT);
    pinMode(ledPin2, OUTPUT);

    // Now create the tasks
    xTaskCreate(blinkLED1, "LED1 Blink", 1000, NULL, 1, NULL);
    xTaskCreate(blinkLED2, "LED2 Blink", 1000, NULL, 1, NULL);
}
