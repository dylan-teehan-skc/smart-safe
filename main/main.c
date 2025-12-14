#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "control_task/control_task.h"
#include "comm_task/comm_task.h"
#include "queue_manager/queue_manager.h"
#include "keypad/keypad.h"
#include "lcd_display/lcd_display.h"

static const char *TAG = "MAIN";

// I2C scanner function for LCD detection
static void i2c_scanner(void)
{
    ESP_LOGI(TAG, "I2C scanner starting...");
    ESP_LOGI(TAG, "Scanning I2C bus on GPIO 21 (SDA) and GPIO 22 (SCL)");
    
    int devices_found = 0;
    
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found I2C device at address: 0x%02X", addr);
            devices_found++;
            
            // Identify known devices
            if (addr == 0x68) {
                ESP_LOGI(TAG, "  -> MPU6050 Accelerometer");
            } else if (addr == 0x3E) {
                ESP_LOGI(TAG, "  -> LCD1602 Controller");
            } else if (addr == 0x60) {
                ESP_LOGI(TAG, "  -> LCD RGB Backlight");
            }
        }
    }
    
    if (devices_found == 0) {
        ESP_LOGW(TAG, "No I2C devices found! Check wiring.");
    } else {
        ESP_LOGI(TAG, "I2C scan complete. Found %d device(s)", devices_found);
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

    // Create control task (high priority) - handles sensors, keypad, LEDs
    if (xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control_task");
        return;
    }
    ESP_LOGI(TAG, "Control task created (priority 5)");
    
    // Wait for MPU6050 to initialize (it sets up I2C bus)
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Run I2C scanner to detect LCD
    i2c_scanner();
    
    // Initialize and test LCD display
    ESP_LOGI(TAG, "Initializing LCD display...");
    if (lcd_display_init()) {
        lcd_display_set_backlight_rgb(255, 0, 0); // Red backlight
        lcd_display_write("Smart Safe", 0);
        lcd_display_write("Initializing...", 1);
        ESP_LOGI(TAG, "LCD display initialized and tested");
    } else {
        ESP_LOGE(TAG, "Failed to initialize LCD display");
    }

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
