#include <Arduino.h>

#define LED_PIN 2

void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    while (!Serial); // Wait for serial monitor
    Serial.println("ESP32 Test Program Starting...");

    // Initialize LED pin
    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    Serial.println("Blinking LED...");
    digitalWrite(LED_PIN, HIGH);  // Turn LED on
    delay(500);                   // Wait 500 ms
    digitalWrite(LED_PIN, LOW);   // Turn LED off
    delay(500);                   // Wait 500 ms
}
