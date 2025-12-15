#include <stdint.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "leds.h"
#include "../queue_manager/queue_manager.h"

// Flash interval for alarm state
#define ALARM_FLASH_INTERVAL_US (500 * 1000)

static const char *TAG = "LED";

static led_mode_t current_mode = LED_MODE_OFF;
static int alarm_led_on = 0;
static int64_t last_toggle_time_us = 0;

static void set_outputs(int red_on, int green_on)
{
    gpio_set_level(RED_LED_PIN, red_on);
    gpio_set_level(GREEN_LED_PIN, green_on);
}

void leds_init(void)
{
    gpio_config_t red_led_conf = {
        .pin_bit_mask = (1ULL << RED_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&red_led_conf);
    gpio_set_level(RED_LED_PIN, 0);

    gpio_config_t green_led_conf = {
        .pin_bit_mask = (1ULL << GREEN_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&green_led_conf);
    gpio_set_level(GREEN_LED_PIN, 0);

    current_mode = LED_MODE_OFF;
    alarm_led_on = 0;
    last_toggle_time_us = esp_timer_get_time();
    ESP_LOGI(TAG, "LEDs initialized: Red=GPIO%d, Green=GPIO%d", RED_LED_PIN, GREEN_LED_PIN);
}

led_mode_t leds_get_mode(void)
{
    return current_mode;
}

void set_locked_led(void)
{
    current_mode = LED_MODE_LOCKED;
    alarm_led_on = 0;
    set_outputs(1, 0);
    ESP_LOGI(TAG, "LED State: LOCKED (Red ON)");
}

void set_unlocked_led(void)
{
    current_mode = LED_MODE_UNLOCKED;
    alarm_led_on = 0;
    set_outputs(0, 1);
    ESP_LOGI(TAG, "LED State: UNLOCKED (Green ON)");
}

void set_alarm_led_flashing(void)
{
    current_mode = LED_MODE_ALARM_FLASH;
    alarm_led_on = 1;
    last_toggle_time_us = esp_timer_get_time();
    set_outputs(1, 0);
    ESP_LOGI(TAG, "LED State: ALARM (Red flashing)");
}

void leds_update(void)
{
    if (current_mode != LED_MODE_ALARM_FLASH) {
        return;
    }

    int64_t now = esp_timer_get_time();
    if ((now - last_toggle_time_us) >= ALARM_FLASH_INTERVAL_US) {
        alarm_led_on = !alarm_led_on;
        last_toggle_time_us = now;
        set_outputs(alarm_led_on, 0);
    }
}

void led_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "LED task started (Priority 3)");

    // Initialize LEDs
    leds_init();
    set_locked_led();  // Default to locked state

    while (1) {
        // Check for LED commands (non-blocking)
        led_cmd_t cmd;
        if (receive_led_cmd(&cmd, 0)) {
            switch (cmd.type) {
                case LED_CMD_LOCKED:
                    set_locked_led();
                    break;
                case LED_CMD_UNLOCKED:
                    set_unlocked_led();
                    break;
                case LED_CMD_ALARM:
                    set_alarm_led_flashing();
                    break;
            }
        }

        // Update alarm flashing animation
        leds_update();

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
