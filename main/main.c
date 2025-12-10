#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "keypad/keypad.h"

#include "control_task/control_task.h"
#include "comm_task/comm_task.h"

#define VIBRATION_SENSOR_PIN GPIO_NUM_36
void app_main(void)
{
    printf("Smart Safe starting...\n");

    // Create control task (high priority) - handles sensors, keypad, LEDs
    if (xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL) == pdFAIL) {
        printf("Error: Failed to create control_task!\n");
        return;
    }

    // Create comm task (lower priority) - handles WiFi, MQTT
    if (xTaskCreate(comm_task, "comm_task", 4096, NULL, 3, NULL) != pdPASS) {
        printf("Error: Failed to create comm_task!\n");
        // Optionally, take further action here (e.g., halt, retry, etc.)
    }
}