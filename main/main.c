#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "keypad/keypad.h"

#include "control_task/control_task.h"
#include "comm_task/comm_task.h"
#include "queue_manager/queue_manager.h"

static const char *TAG = "MAIN";

#define VIBRATION_SENSOR_PIN GPIO_NUM_36
void app_main(void)
{
    ESP_LOGI(TAG, "Smart Safe starting...");

    // Initialize NVS (needed for WiFi and storing PIN)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize queues for inter-task communication
    if (!queue_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize queues");
        return;
    }

    // Create control task (high priority) - handles sensors, keypad, LEDs
    if (xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control_task");
        return;
    }
    ESP_LOGI(TAG, "Control task created (priority 5)");

    // Create comm task (lower priority) - handles WiFi, MQTT
    // NOTE: The comm_task stack size is set to 8192 bytes (double control_task's 4096)
    // This is required due to memory-intensive operations such as cJSON parsing,
    // WiFi and MQTT stack usage, which can cause stack overflows with smaller sizes.
    // Do not reduce this value without thoroughly testing comm_task functionality.
    if (xTaskCreate(comm_task, "comm_task", 8192, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create comm_task");
        return;
    }
    ESP_LOGI(TAG, "Comm task created (priority 3)");

    ESP_LOGI(TAG, "Smart Safe initialized");
}
