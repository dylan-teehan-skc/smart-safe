#include "control_task.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void control_task(void *pvParameters)
{
    (void)pvParameters;
    printf("Control task started\n");

    while (1) {
        printf("Control task running\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
