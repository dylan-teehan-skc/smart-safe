#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>

// Movement detection threshold in g
#define MOVEMENT_THRESHOLD 0.3f

// I2C pins for MPU6050
#define MPU6050_SDA_PIN     26
#define MPU6050_SCL_PIN     25

// Initialize MPU6050 accelerometer
// Returns true on success, false on failure
bool mpu6050_init(void);

// Read acceleration and calculate movement
// Returns movement magnitude in g units
// Returns 0.0 if read fails
float mpu6050_read_movement(void);

// Check if movement exceeds threshold
// Returns true if movement > MOVEMENT_THRESHOLD
bool mpu6050_movement_detected(void);

#endif
