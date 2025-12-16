#include "mpu6050.h"
#include <math.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "../lcd_display/lcd_display.h"
#include "../queue_manager/queue_manager.h"
#include "../config.h"

static const char *TAG = "MPU6050";

// I2C mutex (shared with LCD)
static SemaphoreHandle_t i2c_mutex = NULL;

// Semaphore to signal motion interrupt
static SemaphoreHandle_t motion_semaphore = NULL;

// MPU6050 I2C address
#define MPU6050_ADDR        0x68

// MPU6050 registers
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_PWR_MGMT_2   0x6C
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_INT_PIN_CFG  0x37
#define MPU6050_INT_ENABLE   0x38
#define MPU6050_INT_STATUS   0x3A
#define MPU6050_MOT_THR      0x1F  // Motion detection threshold
#define MPU6050_MOT_DUR      0x20  // Motion detection duration
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_CONFIG       0x1A

// I2C configuration
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         100000

static bool initialized = false;
static int movement_hit_count = 0;
static int32_t movement_threshold = INITIAL_SENSITIVITY;

// ISR handler - uses IRAM_ATTR and FromISR functions for interrupt safety
static void IRAM_ATTR mpu6050_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(motion_semaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t mpu6050_write_reg(uint8_t reg_addr, uint8_t data)
{
    if (i2c_mutex == NULL || xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire I2C mutex for write");
        return ESP_ERR_TIMEOUT;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    xSemaphoreGive(i2c_mutex);
    return ret;
}

static esp_err_t mpu6050_read_reg(uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (len == 0) return ESP_OK;
    
    if (i2c_mutex == NULL || xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire I2C mutex for read");
        return ESP_ERR_TIMEOUT;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    xSemaphoreGive(i2c_mutex);
    return ret;
}

// Configure MPU6050 data ready interrupt for reliable motion detection
static bool mpu6050_configure_interrupt(void)
{
    // Create semaphore for data ready interrupt signaling
    motion_semaphore = xSemaphoreCreateBinary();
    if (motion_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create motion semaphore");
        return false;
    }

    // Configure accelerometer: ±2g range for maximum sensitivity
    if (mpu6050_write_reg(MPU6050_ACCEL_CONFIG, 0x00) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure accelerometer range");
    }

    // Enable DLPF for 1kHz accel output rate (required for data ready interrupt)
    if (mpu6050_write_reg(0x1A, 0x01) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure DLPF");
    }

    // Set sample rate divider for ~50Hz (1kHz / (19+1) = 50Hz)
    if (mpu6050_write_reg(0x19, 19) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set sample rate");
    }

    // Configure INT pin: active low, push-pull, latched, clear on any read
    if (mpu6050_write_reg(MPU6050_INT_PIN_CFG, 0xB0) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure INT pin");
    }

    // Enable Data Ready interrupt (bit 0)
    if (mpu6050_write_reg(MPU6050_INT_ENABLE, 0x01) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable data ready interrupt");
    }

    // Configure ESP32 GPIO for interrupt
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MPU6050_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    // Install GPIO ISR service (may already be installed by keypad)
    esp_err_t isr_ret = gpio_install_isr_service(0);
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_ret));
        return false;
    }

    // Add ISR handler for MPU6050 INT pin
    gpio_isr_handler_add(MPU6050_INT_PIN, mpu6050_isr_handler, NULL);
    gpio_intr_enable(MPU6050_INT_PIN);

    ESP_LOGI(TAG, "Data ready interrupt configured on GPIO %d", MPU6050_INT_PIN);
    return true;
}

bool mpu6050_init(void)
{
    if (initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing MPU6050 with interrupt-driven motion detection...");

    // Get shared I2C mutex from LCD driver
    i2c_mutex = lcd_display_get_i2c_mutex();
    if (i2c_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to get I2C mutex - ensure lcd_display_init() is called first");
        return false;
    }

    // I2C bus is already initialized in main.c
    ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d)", MPU6050_SDA_PIN, MPU6050_SCL_PIN);

    // Wake up MPU6050 (clear sleep bit, use internal 8MHz oscillator)
    if (mpu6050_write_reg(MPU6050_PWR_MGMT_1, 0x00) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to wake MPU6050");
    }

    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for sensor to stabilize

    // Read WHO_AM_I register to verify
    uint8_t who_am_i = 0;
    if (mpu6050_read_reg(MPU6050_WHO_AM_I, &who_am_i, 1) == ESP_OK) {
        ESP_LOGI(TAG, "WHO_AM_I: 0x%02X (expected 0x68)", who_am_i);
        if (who_am_i == 0x68) {
            ESP_LOGI(TAG, "MPU6050 detected successfully");

            // Configure data ready interrupt for software motion detection
            if (!mpu6050_configure_interrupt()) {
                ESP_LOGE(TAG, "Failed to configure interrupt");
                return false;
            }

            initialized = true;
            return true;
        }
    }

    ESP_LOGW(TAG, "MPU6050 not detected - check wiring");
    return false;
}

float mpu6050_read_movement(void)
{
    if (!initialized) {
        return 0.0f;
    }

    uint8_t data[6];
    if (mpu6050_read_reg(MPU6050_ACCEL_XOUT_H, data, 6) != ESP_OK) {
        return 0.0f;
    }

    int16_t accel_x = (int16_t)((data[0] << 8) | data[1]);
    int16_t accel_y = (int16_t)((data[2] << 8) | data[3]);
    int16_t accel_z = (int16_t)((data[4] << 8) | data[5]);

    // Calculate magnitude squared (avoid sqrt for efficiency)
    int32_t magnitude_sq = (accel_x * accel_x) + (accel_y * accel_y) + (accel_z * accel_z);

    // Convert to approximate g value for logging
    float magnitude = sqrtf((float)magnitude_sq) / 16384.0f;

    return magnitude;
}

bool mpu6050_movement_detected(void)
{
    if (!initialized) {
        return false;
    }

    uint8_t data[6];
    if (mpu6050_read_reg(MPU6050_ACCEL_XOUT_H, data, 6) != ESP_OK) {
        return false;
    }

    int16_t accel_x = (int16_t)((data[0] << 8) | data[1]);
    int16_t accel_y = (int16_t)((data[2] << 8) | data[3]);
    int16_t accel_z = (int16_t)((data[4] << 8) | data[5]);

    // Calculate magnitude squared (use int64_t to avoid overflow)
    int64_t magnitude_sq = (int64_t)accel_x * accel_x + (int64_t)accel_y * accel_y + (int64_t)accel_z * accel_z;
    int64_t threshold_sq = (int64_t)movement_threshold * movement_threshold;

    // Debounce: require consecutive over-threshold readings
    if (magnitude_sq > threshold_sq) {
        if (movement_hit_count < MOVEMENT_HIT_COUNT) {
            movement_hit_count++;
        }
    } else if (movement_hit_count > 0) {
        movement_hit_count--;
    }

    if (movement_hit_count >= MOVEMENT_HIT_COUNT) {
        movement_hit_count = 0;  // Reset after confirming movement
        ESP_LOGW(TAG, "Movement detected! X:%d Y:%d Z:%d", accel_x, accel_y, accel_z);
        return true;
    }

    return false;
}

void mpu6050_set_threshold(int32_t threshold)
{
    if (threshold < MOVEMENT_THRESHOLD_MIN) {
        threshold = MOVEMENT_THRESHOLD_MIN;
    } else if (threshold > MOVEMENT_THRESHOLD_MAX) {
        threshold = MOVEMENT_THRESHOLD_MAX;
    }
    movement_threshold = threshold;

    // Convert to MPU6050 MOT_THR format (0-255, ~32mg per LSB)
    // Map 17000-45000 to approximately 10-80 in MOT_THR
    uint8_t mot_thr = (uint8_t)(((threshold - MOVEMENT_THRESHOLD_MIN) * 70) /
                                 (MOVEMENT_THRESHOLD_MAX - MOVEMENT_THRESHOLD_MIN) + 10);
    if (initialized) {
        mpu6050_write_reg(MPU6050_MOT_THR, mot_thr);
    }

    ESP_LOGI(TAG, "Movement threshold set to %ld (MOT_THR=%d)", (long)movement_threshold, mot_thr);
}

int32_t mpu6050_get_threshold(void)
{
    return movement_threshold;
}

void sensor_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Sensor task started - interrupt-driven");

    // Initialize sensor
    if (!mpu6050_init()) {
        ESP_LOGE(TAG, "Failed to initialize MPU6050, task exiting");
        vTaskDelete(NULL);
        return;
    }

    // Register with task watchdog (10 second timeout)
    esp_task_wdt_add(NULL);

    while (1) {
        // Wait for data ready interrupt (50Hz)
        if (xSemaphoreTake(motion_semaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
            esp_task_wdt_reset();

            // Clear interrupt by reading INT_STATUS
            uint8_t int_status = 0;
            mpu6050_read_reg(MPU6050_INT_STATUS, &int_status, 1);

            // Check for movement using software detection
            if (mpu6050_movement_detected()) {
                float movement = mpu6050_read_movement();
                sensor_event_t evt = { .movement_g = movement };
                send_sensor_event(&evt);
                ESP_LOGW(TAG, "Movement %.2fg detected", movement);

                // Debounce delay
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        } else {
            esp_task_wdt_reset();
        }
    }
}
