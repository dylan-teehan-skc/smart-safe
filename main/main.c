#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "keypad.h"

#define VIBRATION_SENSOR_PIN GPIO_NUM_36

// Uncomment to test keypad instead of vibration sensor
#define TEST_KEYPAD

void app_main(void)
{
#ifdef TEST_KEYPAD
    // Keypad test mode
    printf("=== Keypad Test Mode ===\n");
    keypad_init();
    keypad_demo();  // This will loop forever
#else
    // Vibration sensor mode (original functionality)
    printf("=== Vibration Sensor Mode ===\n");
    
    // Configure GPIO 36 as input (no pull-up/down - not supported on GPIO 34-39)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << VIBRATION_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    printf("Vibration sensor monitoring started\n");

    int last_level = 0;

    while (1) {
        int level = gpio_get_level(VIBRATION_SENSOR_PIN);

        // Detect rising edge (0 -> 1)
        if (level == 1 && last_level == 0) {
            printf("Vibration detected!\n");
        }

        last_level = level;
        vTaskDelay(10 / portTICK_PERIOD_MS);  // Check every 10ms
    }
#endif
}