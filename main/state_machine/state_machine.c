#include <stdio.h>
#include "esp_timer.h"
#include "state_machine.h"

#define MAX_WRONG_ATTEMPTS 3

safe_state_machine_t state_machine_init(void)
{
    safe_state_machine_t sm;
    sm.current_state = STATE_LOCKED;
    sm.wrong_count = 0;
    return sm;
}

static void set_locked(safe_state_machine_t *sm)
{
    sm->current_state = STATE_LOCKED;
    sm->wrong_count = 0;
}

static void set_unlocked(safe_state_machine_t *sm)
{
    sm->current_state = STATE_UNLOCKED;
    sm->wrong_count = 0;
}

static void set_alarm(safe_state_machine_t *sm)
{
    sm->current_state = STATE_ALARM;
}

safe_state_t state_machine_process_event(safe_state_machine_t *sm, safe_event_t event)
{
    if (!sm) return STATE_LOCKED;

    switch (sm->current_state) {
        case STATE_LOCKED:
            if (event == EVENT_CORRECT_PIN) {
                set_unlocked(sm);
            } else if (event == EVENT_WRONG_PIN) {
                sm->wrong_count++;
                if (sm->wrong_count >= MAX_WRONG_ATTEMPTS) {
                    set_alarm(sm);
                }
            } else if (event == EVENT_MOVEMENT) {
                // Movement detected while locked - trigger alarm
                set_alarm(sm);
            }
            break;

        case STATE_UNLOCKED:
            if (event == EVENT_CORRECT_PIN) {
                // Toggle back to locked when correct PIN entered while unlocked
                set_locked(sm);
            }
            // Wrong PINs are ignored when unlocked (no counting, no alarm)
            // Movement is ignored when unlocked (user is accessing the safe)
            break;

        case STATE_ALARM:
            if (event == EVENT_CORRECT_PIN) {
                set_locked(sm);
            }
            // Wrong PINs are ignored in alarm state (must use correct PIN to reset)
            break;

        default:
            set_locked(sm);
            break;
    }

    return sm->current_state;
}


safe_state_t state_machine_get_state(const safe_state_machine_t *sm)
{
    if (!sm) return STATE_LOCKED;
    return sm->current_state;
}

uint8_t state_machine_get_wrong_count(const safe_state_machine_t *sm)
{
    if (!sm) return 0;
    return sm->wrong_count;
}

