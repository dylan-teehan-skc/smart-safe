#include <stdio.h>
#include <stdint.h>
#include "esp_timer.h"
#include "leds.h"

static led_mode_t current_mode = LED_MODE_OFF;
static int alarm_led_on = 0;
static int64_t last_toggle_time_us = 0;
#define ALARM_FLASH_INTERVAL_US (500 * 1000)

void leds_init(void)
{
    current_mode = LED_MODE_OFF;
    alarm_led_on = 0;
    last_toggle_time_us = esp_timer_get_time();
}

led_mode_t leds_get_mode(void)
{
    return current_mode;
}

static void show_led_state(void)
{
    switch (current_mode) {
        case LED_MODE_LOCKED:
            printf("[LED] Red ON (LOCKED)\n");
            break;
        case LED_MODE_UNLOCKED:
            printf("[LED] Green ON (UNLOCKED)\n");
            break;
        case LED_MODE_ALARM_FLASH:
            if (alarm_led_on) {
                printf("[LED] Red ON (ALARM FLASH)\n");
            } else {
                printf("[LED] Red OFF (ALARM FLASH)\n");
            }
            break;
        case LED_MODE_OFF:
        default:
            printf("[LED] All OFF\n");
            break;
    }
}

void set_locked_led(void)
{
    current_mode = LED_MODE_LOCKED;
    alarm_led_on = 0;
    show_led_state();
}

void set_unlocked_led(void)
{
    current_mode = LED_MODE_UNLOCKED;
    alarm_led_on = 0;
    show_led_state();
}

void set_alarm_led_flashing(void)
{
    current_mode = LED_MODE_ALARM_FLASH;
    alarm_led_on = 1;
    last_toggle_time_us = esp_timer_get_time();
    show_led_state();
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
        show_led_state();
    }
}
