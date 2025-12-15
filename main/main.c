#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "queue_manager/queue_manager.h"
#include "keypad/keypad.h"
#include "mpu6050/mpu6050.h"
#include "led/leds.h"
#include "lcd_display/lcd_display.h"
#include "control_task/control_task.h"
#include "comm_task/comm_task.h"

static const char *TAG = "MAIN";

// I2C configuration (shared by MPU6050 and LCD)
#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_FREQ_HZ          100000
#define I2C_MASTER_NUM              I2C_NUM_0

// Task priorities (higher number = higher priority)
// See docs/task-architecture.md for detailed rationale
#define KEYPAD_TASK_PRIORITY    6   // Highest - user expects instant response
#define SENSOR_TASK_PRIORITY    5   // Security critical tamper detection
#define CONTROL_TASK_PRIORITY   4   // Central logic coordinator
#define LED_TASK_PRIORITY       3   // Real-time 500ms alarm flash
#define LCD_TASK_PRIORITY       2   // Slow I2C, non-critical timing
#define COMM_TASK_PRIORITY      1   // Lowest - network can be slow

// Task stack sizes
// NOTE: Stack sizes account for NVS operations, logging, and library usage
#define KEYPAD_TASK_STACK   2048
#define SENSOR_TASK_STACK   2048
#define CONTROL_TASK_STACK  8192    // Needs extra for NVS operations in pin_manager
#define LED_TASK_STACK      2048
#define LCD_TASK_STACK      3072    // I2C operations need extra stack
#define COMM_TASK_STACK     8192

static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C master initialized (SDA=%d, SCL=%d)", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    return ESP_OK;
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

    // Initialize I2C bus (shared by MPU6050 and LCD)
    ESP_LOGI(TAG, "Initializing I2C bus...");
    ESP_ERROR_CHECK(i2c_master_init());

    // Initialize queues for inter-task communication
    // Creates 6 queues: key_queue, sensor_queue, led_queue, lcd_queue, event_queue, cmd_queue
    if (!queue_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize queues");
        return;
    }

    // Initialize keypad GPIO and ISR (must be done before keypad_task starts)
    keypad_init();

    ESP_LOGI(TAG, "Creating 6 FreeRTOS tasks...");

    // Task creation - lowest to highest priority

    // Priority 1 (lowest): Comm task - handles WiFi, MQTT
    // Can tolerate delays without affecting safe operation
    if (xTaskCreate(comm_task, "comm_task", COMM_TASK_STACK, NULL, COMM_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create comm_task");
        return;
    }
    ESP_LOGI(TAG, "  comm_task created (priority %d)", COMM_TASK_PRIORITY);

    // Priority 2: LCD task - handles display updates
    // Slow I2C writes, non-critical timing
    if (xTaskCreate(lcd_task, "lcd_task", LCD_TASK_STACK, NULL, LCD_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create lcd_task");
        return;
    }
    ESP_LOGI(TAG, "  lcd_task created (priority %d)", LCD_TASK_PRIORITY);

    // Priority 3: LED task - handles LED state and alarm flashing
    // Real-time 500ms flash animation for alarm state
    if (xTaskCreate(led_task, "led_task", LED_TASK_STACK, NULL, LED_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create led_task");
        return;
    }
    ESP_LOGI(TAG, "  led_task created (priority %d)", LED_TASK_PRIORITY);

    // Priority 4: Control task - state machine, PIN verification, command handling
    // Central coordinator that processes all inputs and makes decisions
    if (xTaskCreate(control_task, "control_task", CONTROL_TASK_STACK, NULL, CONTROL_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control_task");
        return;
    }
    ESP_LOGI(TAG, "  control_task created (priority %d)", CONTROL_TASK_PRIORITY);

    // Priority 5: Sensor task - MPU6050 accelerometer polling
    // Security critical - tamper detection must not be delayed
    if (xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK, NULL, SENSOR_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor_task");
        return;
    }
    ESP_LOGI(TAG, "  sensor_task created (priority %d)", SENSOR_TASK_PRIORITY);

    // Priority 6 (highest): Keypad task - handles user input
    // User expects immediate response to key presses
    if (xTaskCreate(keypad_task, "keypad_task", KEYPAD_TASK_STACK, NULL, KEYPAD_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create keypad_task");
        return;
    }
    ESP_LOGI(TAG, "  keypad_task created (priority %d)", KEYPAD_TASK_PRIORITY);

    ESP_LOGI(TAG, "Smart Safe initialized with 6 tasks");
}
