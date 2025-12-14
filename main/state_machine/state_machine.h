#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include "../queue_manager/queue_manager.h"

typedef enum {
    EVENT_CORRECT_PIN = 0,
    EVENT_WRONG_PIN,
    EVENT_MOVEMENT,  // Movement detected by accelerometer
} safe_event_t;

typedef struct {
    safe_state_t current_state;
    uint8_t wrong_count;
} safe_state_machine_t;
/**
 * @brief Initialize the state machine
 * 
 * Must be called before using any other state_machine functions.
 * Must only be called from control task.
 */
safe_state_machine_t state_machine_init(void);
safe_state_t state_machine_process_event(safe_state_machine_t *sm, safe_event_t event);
safe_state_t state_machine_get_state(const safe_state_machine_t *sm);
uint8_t state_machine_get_wrong_count(const safe_state_machine_t *sm);

#endif
