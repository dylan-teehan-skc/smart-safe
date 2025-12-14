#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "../queue_manager/queue_manager.h"
#include "../state_machine/state_machine.h"

/**
 * @brief Process a remote command
 * 
 * Handles lock, unlock, set_code, and reset_alarm commands.
 * Updates LEDs and publishes state change events as needed.
 * 
 * @param cmd Pointer to the command to process
 * @param sm Pointer to the safe state machine
 */
void command_handler_process(command_t *cmd, safe_state_machine_t *sm);

#endif // COMMAND_HANDLER_H
