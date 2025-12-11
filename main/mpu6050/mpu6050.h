#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>

// Movement detection threshold (raw LSB magnitude squared)
#define MOVEMENT_THRESHOLD_RAW 20000
#define MOVEMENT_HIT_COUNT 3  // Consecutive hits to confirm movement

// I2C pins for MPU6050
#define MPU6050_SDA_PIN     21
#define MPU6050_SCL_PIN     22

// Initialize MPU6050 accelerometer
// Returns true on success, false on failure
bool mpu6050_init(void);

// Read acceleration and calculate movement
// Returns movement magnitude in g units
// Returns 0.0 if read fails
float mpu6050_read_movement(void);

// Check if movement exceeds threshold
// Returns true if movement > threshold (with debouncing)
bool mpu6050_movement_detected(void);

#endif
