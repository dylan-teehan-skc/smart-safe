#include "mpu6050.h"
#include <math.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../lcd_display/lcd_display.h"

static const char *TAG = "MPU6050";

// I2C mutex (shared with LCD)
static SemaphoreHandle_t i2c_mutex = NULL;

// MPU6050 I2C address
#define MPU6050_ADDR        0x68

// MPU6050 registers
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_ACCEL_XOUT_H 0x3B

// I2C configuration
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         100000

static bool initialized = false;
static int movement_hit_count = 0;

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

bool mpu6050_init(void)
{
    if (initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing MPU6050...");

    // Get shared I2C mutex from LCD driver
    i2c_mutex = lcd_display_get_i2c_mutex();
    if (i2c_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to get I2C mutex - ensure lcd_display_init() is called first");
        return false;
    }

    // I2C bus is already initialized in main.c
    ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d)", MPU6050_SDA_PIN, MPU6050_SCL_PIN);

    // Wake up MPU6050 (clear sleep bit)
    if (mpu6050_write_reg(MPU6050_PWR_MGMT_1, 0x00) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to wake MPU6050");
    }

    // Read WHO_AM_I register to verify
    uint8_t who_am_i = 0;
    if (mpu6050_read_reg(MPU6050_WHO_AM_I, &who_am_i, 1) == ESP_OK) {
        ESP_LOGI(TAG, "WHO_AM_I: 0x%02X (expected 0x68)", who_am_i);
        if (who_am_i == 0x68) {
            ESP_LOGI(TAG, "MPU6050 detected successfully");
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

    // Calculate magnitude squared
    int32_t magnitude_sq = (accel_x * accel_x) + (accel_y * accel_y) + (accel_z * accel_z);
    int32_t threshold_sq = (int32_t)MOVEMENT_THRESHOLD_RAW * MOVEMENT_THRESHOLD_RAW;

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
