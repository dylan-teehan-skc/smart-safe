#include "control_task.h"
#include <stdio.h>
#include <string.h>
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
#include "../state_machine/state_machine.h"
#include "../led/leds.h"

#define CORRECT_PIN "1234"

static safe_state_machine_t safe_sm;

static const char* state_to_string(safe_state_t state)
{
    switch (state) {
        case STATE_LOCKED:
            return "LOCKED";
        case STATE_UNLOCKED:
            return "UNLOCKED";
        case STATE_ALARM:
            return "ALARM";
        default:
            return "UNKNOWN";
    }
}

static void process_pin_entry(const char *pin)
{
    printf("\n[PIN]: %s\n", pin);

    if (strcmp(pin, CORRECT_PIN) == 0) {
        printf("[SM]: Correct PIN\n");
        safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_CORRECT_PIN);
        printf("[STATE]: -> %s\n", state_to_string(new_state));

        if (new_state == STATE_UNLOCKED) {
            set_unlocked_led();
        } else if (new_state == STATE_LOCKED) {
            set_locked_led();
        }
    } else {
        printf("[SM]: Wrong PIN\n");
        safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_WRONG_PIN);
        uint8_t wrong_count = state_machine_get_wrong_count(&safe_sm);
        printf("[COUNT]: %d/3\n", wrong_count);
        printf("[STATE]: -> %s\n", state_to_string(new_state));

        if (new_state == STATE_ALARM) {
            set_alarm_led_flashing();
        }
    }
}

void control_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "\nControl task started");

    // Send initial state
    notify_state_change();
    
    leds_init();
    set_locked_led();
    safe_sm = state_machine_init();
    printf("State machine ready: %s\n\n", state_to_string(safe_sm.current_state));

    while (1) {
        // Check for incoming commands (non-blocking)
        command_t cmd;
        if (receive_command(&cmd, 0)) {
            handle_incoming_command(&cmd);
        } else {
            // Block until interrupt semaphore or command received
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // TODO: Keypad - use GPIO interrupt on column pins, ISR gives semaphore
        // TODO: Vibration sensor - use GPIO interrupt, ISR gives semaphore
        // TODO: Update LEDs based on state
        // TODO: Handle auto-lock timeout using FreeRTOS software timer
    }
}
