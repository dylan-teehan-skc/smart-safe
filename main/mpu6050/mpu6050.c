#include "mpu6050.h"
#include <math.h>
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "MPU6050";

// MPU6050 I2C address
#define MPU6050_ADDR        0x68

// MPU6050 registers
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B

// I2C port
#define I2C_PORT I2C_NUM_0

// Last readings for movement calculation
static float last_ax = 0, last_ay = 0, last_az = 0;
static bool initialized = false;

static esp_err_t i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MPU6050_SDA_PIN,
        .scl_io_num = MPU6050_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000
    };

    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) return err;

    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t mpu6050_write_byte(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    return i2c_master_write_to_device(I2C_PORT, MPU6050_ADDR, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t mpu6050_read_bytes(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_PORT, MPU6050_ADDR, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

bool mpu6050_init(void)
{
    if (initialized) {
        return true;
    }

    // Initialize I2C
    esp_err_t err = i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d)", MPU6050_SDA_PIN, MPU6050_SCL_PIN);

    // Wake up MPU6050 (clear sleep bit)
    err = mpu6050_write_byte(MPU6050_PWR_MGMT_1, 0x00);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 wake-up failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "MPU6050 initialized");
    initialized = true;
    return true;
}

float mpu6050_read_movement(void)
{
    if (!initialized) {
        return 0.0f;
    }

    uint8_t data[6];
    esp_err_t err = mpu6050_read_bytes(MPU6050_ACCEL_XOUT_H, data, 6);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(err));
        return 0.0f;
    }

    int16_t accel_x = (data[0] << 8) | data[1];
    int16_t accel_y = (data[2] << 8) | data[3];
    int16_t accel_z = (data[4] << 8) | data[5];

    // Convert to g (default scale is +/-2g, 16384 LSB/g)
    float ax = accel_x / 16384.0f;
    float ay = accel_y / 16384.0f;
    float az = accel_z / 16384.0f;

    // Calculate change from last reading
    float dx = ax - last_ax;
    float dy = ay - last_ay;
    float dz = az - last_az;
    float movement = sqrtf(dx*dx + dy*dy + dz*dz);

    // Store current readings
    last_ax = ax;
    last_ay = ay;
    last_az = az;

    return movement;
}

bool mpu6050_movement_detected(void)
{
    float movement = mpu6050_read_movement();
    if (movement > MOVEMENT_THRESHOLD) {
        ESP_LOGW(TAG, "Movement detected: %.2fg", movement);
        return true;
    }
    return false;
}
