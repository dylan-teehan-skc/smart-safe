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

// I2C configuration (shared by MPU6050 and LCD)
#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_FREQ_HZ          100000
#define I2C_MASTER_NUM              I2C_NUM_0

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

    // Initialize queues for inter-task communication
    if (!queue_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize queues");
        return;
    }

    // Initialize keypad
    keypad_init();

    // Initialize I2C bus (shared by MPU6050 and LCD)
    ESP_LOGI(TAG, "Initializing I2C bus...");
    ESP_ERROR_CHECK(i2c_master_init());

    // Initialize LCD display (state machine will update it when ready)
    ESP_LOGI(TAG, "Initializing LCD display...");
    if (lcd_display_init()) {
        ESP_LOGI(TAG, "LCD display initialized");
    } else {
        ESP_LOGE(TAG, "Failed to initialize LCD display");
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
