#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_LOCKED,
    LED_MODE_UNLOCKED,
    LED_MODE_ALARM_FLASH,
} led_mode_t;

void leds_init(void);
void set_locked_led(void);
void set_unlocked_led(void);
void set_alarm_led_flashing(void);
void leds_update(void);
led_mode_t leds_get_mode(void);

#endif
