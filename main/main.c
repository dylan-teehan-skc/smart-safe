#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "control_task/control_task.h"
#include "comm_task/comm_task.h"
#include "queue_manager/queue_manager.h"
#include "keypad/keypad.h"

static const char *TAG = "MAIN";

void keypad_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "=== KEYPAD TEST MODE ===");
    ESP_LOGI(TAG, "Press keys on the keypad to test.");
    ESP_LOGI(TAG, "Each key press should be logged below.");
    ESP_LOGI(TAG, "========================");
    
    while (1) {
        // Non-blocking check for key press
        char key = keypad_get_key();
        
        if (key != '\0') {
            ESP_LOGI(TAG, "Key pressed: '%c'", key);
            
            // Optional: provide feedback for different key types
            if (key >= '0' && key <= '9') {
                ESP_LOGI(TAG, "  -> Digit key");
            } else if (key == '*') {
                ESP_LOGI(TAG, "  -> Clear key");
            } else if (key == '#') {
                ESP_LOGI(TAG, "  -> Submit key");
            } else if (key >= 'A' && key <= 'D') {
                ESP_LOGI(TAG, "  -> Function key");
            }
        }
        
        // Small delay to prevent busy-waiting
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

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

    // Initialize keypad
    keypad_init();

    // PHASE 1: Create simple keypad test task instead of full tasks
    if (xTaskCreate(keypad_test_task, "keypad_test", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create keypad test task");
        return;
    }
    ESP_LOGI(TAG, "Keypad test task created");

    // TEMPORARILY DISABLED: Comment out control and comm tasks for Phase 1 testing
    /*
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
    */

    ESP_LOGI(TAG, "Smart Safe initialized - Keypad Test Mode");
}
