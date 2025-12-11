#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>

typedef enum {
    STATE_LOCKED = 0,
    STATE_UNLOCKED,
    STATE_ALARM,
} safe_state_t;

typedef enum {
    EVENT_CORRECT_PIN = 0,
    EVENT_WRONG_PIN,
    EVENT_VIBRATION,
} safe_event_t;

typedef struct {
    safe_state_t current_state;
    uint8_t wrong_count;
} safe_state_machine_t;

safe_state_machine_t state_machine_init(void);
safe_state_t state_machine_process_event(safe_state_machine_t *sm, safe_event_t event);
safe_state_t state_machine_get_state(const safe_state_machine_t *sm);
uint8_t state_machine_get_wrong_count(const safe_state_machine_t *sm);

#endif
