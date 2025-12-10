#ifndef QUEUE_MANAGER_H
#define QUEUE_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>
#include <stdbool.h>

// Event types (Control Task -> Comm Task)
typedef enum {
    EVT_STATE_CHANGE,    // Safe state changed (locked/unlocked/alarm)
    EVT_VIBRATION,       // Vibration detected
    EVT_CODE_RESULT      // PIN entry result (correct/incorrect)
} event_type_t;

// Safe states
typedef enum {
    STATE_LOCKED,
    STATE_UNLOCKED,
    STATE_ALARM
} safe_state_t;

// Event message sent from Control to Comm
typedef struct {
    event_type_t type;
    uint32_t timestamp;
    safe_state_t state;
    bool vibration;
    bool code_ok;
} event_t;

// Command types (Comm Task -> Control Task)
typedef enum {
    CMD_LOCK,            // Lock the safe
    CMD_UNLOCK,          // Unlock the safe
    CMD_SET_CODE         // Change the PIN code
} command_type_t;

// Command message sent from Comm to Control
typedef struct {
    command_type_t type;
    char code[8];        // New PIN for CMD_SET_CODE
} command_t;

// Queue handles
extern QueueHandle_t control_to_comm_queue;  // Events: Control -> Comm
extern QueueHandle_t comm_to_control_queue;  // Commands: Comm -> Control

// Initialize both queues - call from app_main()
void queue_manager_init(void);

// Send event from Control Task to Comm Task
bool send_event(event_t *event);

// Receive event in Comm Task (blocks for timeout_ms)
bool receive_event(event_t *event, uint32_t timeout_ms);

// Send command from Comm Task to Control Task
bool send_command(command_t *cmd);

// Receive command in Control Task (blocks for timeout_ms)
bool receive_command(command_t *cmd, uint32_t timeout_ms);

#endif
