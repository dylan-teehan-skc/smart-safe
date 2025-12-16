#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>
#include <stdint.h>

// Movement detection threshold defaults (raw LSB magnitude)
// Note: At rest, gravity reads ~16384 LSB (1g), so min must be above this
// Max is 45000 to avoid int32_t overflow when squared (45000^2 < 2^31)
#define MOVEMENT_THRESHOLD_DEFAULT 20000
#define MOVEMENT_THRESHOLD_MIN     17000  // Most sensitive (just above 1g gravity)
#define MOVEMENT_THRESHOLD_MAX     45000  // Least sensitive
#define MOVEMENT_HIT_COUNT 3  // Consecutive hits to confirm movement

// I2C pins for MPU6050
#define MPU6050_SDA_PIN     21
#define MPU6050_SCL_PIN     22

// Interrupt pin for motion detection (MPU6050 INT -> ESP32 GPIO)
#define MPU6050_INT_PIN     16

// Initialize MPU6050 accelerometer
bool mpu6050_init(void);

// Read movement magnitude in g units
float mpu6050_read_movement(void);

// Check if movement exceeds threshold (with debouncing)
bool mpu6050_movement_detected(void);

// Set/get movement sensitivity threshold (17000-45000, lower = more sensitive)
void mpu6050_set_threshold(int32_t threshold);
int32_t mpu6050_get_threshold(void);

// FreeRTOS task for motion detection using hardware interrupts
// Priority 5 - security critical tamper detection
// Uses MPU6050 INT pin (GPIO 19) for interrupt-driven detection
// Task blocks on semaphore until motion interrupt fires
void sensor_task(void *pvParameters);

#endif
