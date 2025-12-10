#include "control_task.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "../queue_manager/queue_manager.h"

static const char *TAG = "CTRL";

// Current safe state
static safe_state_t current_state = STATE_LOCKED;

// Get current timestamp in seconds
static uint32_t get_timestamp(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

// Send state change event to comm task
static void notify_state_change(void)
{
    event_t event = {
        .type = EVT_STATE_CHANGE,
        .timestamp = get_timestamp(),
        .state = current_state,
        .vibration = false,
        .code_ok = false
    };
    send_event(&event);
    ESP_LOGI(TAG, "State changed to: %d", current_state);
}

// Handle incoming commands from comm task
static void handle_incoming_command(command_t *cmd)
{
    switch (cmd->type) {
        case CMD_LOCK:
            ESP_LOGI(TAG, "Received LOCK command");
            if (current_state == STATE_UNLOCKED) {
                current_state = STATE_LOCKED;
                notify_state_change();
            }
            break;

        case CMD_UNLOCK:
            ESP_LOGI(TAG, "Received UNLOCK command");
            if (current_state == STATE_LOCKED || current_state == STATE_ALARM) {
                current_state = STATE_UNLOCKED;
                notify_state_change();
            }
            break;

        case CMD_SET_CODE:
            ESP_LOGI(TAG, "Received SET_CODE command: %s", cmd->code);
            // TODO: Store new code in NVS
            break;

        case CMD_RESET_ALARM:
            ESP_LOGI(TAG, "Received RESET_ALARM command");
            if (current_state == STATE_ALARM) {
                current_state = STATE_LOCKED;
                notify_state_change();
            }
            break;
    }
}

void control_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Control task started");

    // Send initial state
    notify_state_change();

    while (1) {
        // Check for incoming commands (non-blocking)
        command_t cmd;
        if (receive_command(&cmd, 0)) {
            handle_incoming_command(&cmd);
        }

        // TODO: Keypad - use GPIO interrupt on column pins, ISR gives semaphore
        // TODO: Vibration sensor - use GPIO interrupt, ISR gives semaphore
        // TODO: Update LEDs based on state
        // TODO: Handle auto-lock timeout using FreeRTOS software timer

        // Block until interrupt semaphore or command received
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
