#ifndef EVENT_PUBLISHER_H
#define EVENT_PUBLISHER_H

#include <stdint.h>
#include "../state_machine/state_machine.h"

/**
 * @brief Initialize the event publisher
 * 
 * Must be called before using any other event_publisher functions.
 */
void event_publisher_init(void);

/**
 * @brief Publish a state change event
 * 
 * @param sm Pointer to the safe state machine
 */
void event_publisher_state_change(safe_state_machine_t *sm);

/**
 * @brief Publish a movement detection event
 * 
 * @param sm Pointer to the safe state machine
 * @param movement Movement amount in g-force
 */
void event_publisher_movement(safe_state_machine_t *sm, float movement);

/**
 * @brief Publish a code entry result event
 * 
 * @param sm Pointer to the safe state machine
 * @param correct True if the code was correct, false otherwise
 */
void event_publisher_code_result(safe_state_machine_t *sm, bool correct);

/**
 * @brief Publish a code changed event
 * 
 * @param sm Pointer to the safe state machine
 * @param success True if the code change succeeded, false otherwise
 */
void event_publisher_code_changed(safe_state_machine_t *sm, bool success);

#endif // EVENT_PUBLISHER_H
