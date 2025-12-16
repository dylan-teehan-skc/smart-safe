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
// IMPORTANT: Use 220-330 ohm series resistors to limit current!
// Without resistors, LEDs may be damaged and GPIO pins may be overloaded.
// ESP32 GPIO pins can source/sink max 40mA, but 10-20mA is recommended.
#define RED_LED_PIN   GPIO_NUM_4
#define GREEN_LED_PIN GPIO_NUM_18

void leds_init(void);
void set_locked_led(void);
void set_unlocked_led(void);
void set_alarm_led_flashing(void);
void leds_update(void);

// FreeRTOS task that receives LED commands and handles alarm flashing
// Priority 3 - real-time 500ms flash animation
void led_task(void *pvParameters);

#endif
