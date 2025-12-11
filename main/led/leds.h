#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>
#include "driver/gpio.h"

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_LOCKED,
    LED_MODE_UNLOCKED,
    LED_MODE_ALARM_FLASH,
} led_mode_t;

// GPIO pin definitions for LEDs
// IMPORTANT: LEDs must be wired with series resistors between GPIO and LED anode.
#define RED_LED_PIN   GPIO_NUM_4
#define GREEN_LED_PIN GPIO_NUM_18

void leds_init(void);
void set_locked_led(void);
void set_unlocked_led(void);
void set_alarm_led_flashing(void);
void leds_update(void);
led_mode_t leds_get_mode(void);

#endif
