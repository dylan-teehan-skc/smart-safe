#include "comm_task.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void comm_task(void *pvParameters)
{
    printf("Comm task started\n");

    while (1) {
        printf("Comm task running\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
