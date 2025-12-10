#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "control_task/control_task.h"
#include "comm_task/comm_task.h"

void app_main(void)
{
    printf("Smart Safe starting...\n");

    // Create control task (high priority) - handles sensors, keypad, LEDs
    xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);

    // Create comm task (lower priority) - handles WiFi, MQTT
    xTaskCreate(comm_task, "comm_task", 4096, NULL, 3, NULL);
}
