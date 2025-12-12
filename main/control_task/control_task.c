#include "control_task.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "../queue_manager/queue_manager.h"
#include "../state_machine/state_machine.h"
#include "../led/leds.h"
#include "../json_protocol/json_protocol.h"
#include "../config.h"
#include "../mpu6050/mpu6050.h"
#include "../keypad/keypad.h"

static const char *TAG = "CTRL";

// PIN configuration
#define PIN_LENGTH 4  // Expected PIN length (4 digits)

// Get current timestamp in seconds
static uint32_t get_timestamp(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

// Send state change event to comm task
static void notify_state_change(safe_state_machine_t *sm)
{
    event_t event = {
        .type = EVT_STATE_CHANGE,
        .timestamp = get_timestamp(),
        .state = sm->current_state,
        .movement_amount = 0.0f,
        .code_ok = false
    };
    send_event(&event);
    ESP_LOGI(TAG, "State changed to: %s", state_to_string(sm->current_state));
}

// Send movement event to comm task
static void notify_movement(safe_state_machine_t *sm, float movement)
{
    event_t event = {
        .type = EVT_MOVEMENT,
        .timestamp = get_timestamp(),
        .state = sm->current_state,
        .movement_amount = movement,
        .code_ok = false
    };
    send_event(&event);
    ESP_LOGW(TAG, "Movement detected: %.2fg", movement);
}

static safe_state_machine_t safe_sm;

// PIN entry buffer
static char pin_buffer[MAX_PIN_LENGTH] = {0};
static int pin_index = 0;

// Constant-time string comparison to prevent timing attacks
static int constant_time_strcmp(const char *a, const char *b)
{
    int diff = 0;
    size_t i = 0;
    // Compare all characters, don't short-circuit on first difference
    while (a[i] != '\0' && b[i] != '\0') {
        diff |= a[i] ^ b[i];
        i++;
    }
    // Also check if lengths differ
    diff |= a[i] ^ b[i];
    return diff;
}

static void process_pin_entry(const char *pin)
{
    // Use constant-time comparison to prevent timing attacks
    // Do not print the PIN to avoid exposing it in logs
    if (constant_time_strcmp(pin, CORRECT_PIN) == 0) {
        ESP_LOGI(TAG, "Correct PIN entered");
        safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_CORRECT_PIN);
        ESP_LOGI(TAG, "State: %s", state_to_string(new_state));

        if (new_state == STATE_UNLOCKED) {
            set_unlocked_led();
        } else if (new_state == STATE_LOCKED) {
            set_locked_led();
        }
        
        // Send code result event
        event_t event = {
            .type = EVT_CODE_RESULT,
            .timestamp = get_timestamp(),
            .state = safe_sm.current_state,
            .movement_amount = 0.0f,
            .code_ok = true
        };
        send_event(&event);
        
        // Notify state change
        notify_state_change(&safe_sm);
    } else {
        ESP_LOGI(TAG, "Wrong PIN entered");
        safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_WRONG_PIN);
        uint8_t wrong_count = state_machine_get_wrong_count(&safe_sm);
        ESP_LOGI(TAG, "Wrong attempts: %d/3", wrong_count);
        ESP_LOGI(TAG, "State: %s", state_to_string(new_state));

        if (new_state == STATE_ALARM) {
            set_alarm_led_flashing();
            notify_state_change(&safe_sm);
        }
        
        // Send code result event
        event_t event = {
            .type = EVT_CODE_RESULT,
            .timestamp = get_timestamp(),
            .state = safe_sm.current_state,
            .movement_amount = 0.0f,
            .code_ok = false
        };
        send_event(&event);
    }
}

// Clear PIN buffer
static void clear_pin_buffer(void)
{
    memset(pin_buffer, 0, sizeof(pin_buffer));
    pin_index = 0;
    ESP_LOGI(TAG, "PIN cleared");
}

// Handle individual key press
static void handle_key_press(char key)
{
    // Handle digit keys (0-9)
    if (key >= '0' && key <= '9') {
        if (pin_index < PIN_LENGTH) {  // Only accept PIN_LENGTH digits
            pin_buffer[pin_index++] = key;
            pin_buffer[pin_index] = '\0';  // Null terminate
            
            // Log masked PIN (show asterisks for security)
            ESP_LOGI(TAG, "PIN entry: %.*s", pin_index, "****");
        } else {
            ESP_LOGW(TAG, "PIN buffer full (%d digits max)", PIN_LENGTH);
        }
    }
    // Handle clear key (*)
    else if (key == '*') {
        clear_pin_buffer();
    }
    // Handle submit key (#)
    else if (key == '#') {
        if (pin_index == PIN_LENGTH) {
            ESP_LOGI(TAG, "PIN submitted (%d digits)", PIN_LENGTH);
            process_pin_entry(pin_buffer);
        } else if (pin_index > 0) {
            ESP_LOGW(TAG, "PIN too short (%d digits, need %d)", pin_index, PIN_LENGTH);
        }
        // Clear buffer after submission attempt
        clear_pin_buffer();
    }
    // Handle function keys (A-D) - reserved for future use
    else if (key >= 'A' && key <= 'D') {
        ESP_LOGI(TAG, "Function key '%c' pressed (not implemented)", key);
    }
}

void control_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "\nControl task started");

    // Initialize LEDs
    leds_init();
    set_locked_led();

    // Initialize accelerometer
    if (!mpu6050_init()) {
        ESP_LOGE(TAG, "Failed to initialize MPU6050 accelerometer");
    }

    // Initialize state machine
    safe_sm = state_machine_init();
    ESP_LOGI(TAG, "State machine initialized: %s", state_to_string(safe_sm.current_state));

    // Send initial state
    notify_state_change(&safe_sm);
    
    ESP_LOGI(TAG, "Ready for PIN entry:");
    ESP_LOGI(TAG, "  - Press 0-9 to enter digits");
    ESP_LOGI(TAG, "  - Press # to submit PIN");
    ESP_LOGI(TAG, "  - Press * to clear");

    while (1) {
        // Check for keypad input (non-blocking)
        char key = keypad_get_key();
        if (key != '\0') {
            handle_key_press(key);
        }
        
        // Check for incoming commands (non-blocking)
        command_t cmd;
        if (receive_command(&cmd, 0)) {
            // Process incoming commands
            switch (cmd.type) {
                case CMD_LOCK:
                    ESP_LOGI(TAG, "Received LOCK command");
                    if (safe_sm.current_state == STATE_UNLOCKED) {
                        state_machine_process_event(&safe_sm, EVENT_CORRECT_PIN);
                        safe_sm.current_state = STATE_LOCKED;
                        set_locked_led();
                        notify_state_change(&safe_sm);
                    }
                    break;

                case CMD_UNLOCK:
                    ESP_LOGI(TAG, "Received UNLOCK command");
                    if (safe_sm.current_state == STATE_LOCKED) {
                        safe_sm.current_state = STATE_UNLOCKED;
                        set_unlocked_led();
                        notify_state_change(&safe_sm);
                    }
                    break;

                case CMD_SET_CODE:
                    ESP_LOGI(TAG, "Received SET_CODE command: %s", cmd.code);
                    // TODO: Store new code in NVS
                    {
                        event_t event = {
                            .type = EVT_CODE_CHANGED,
                            .timestamp = get_timestamp(),
                            .state = safe_sm.current_state,
                            .movement_amount = 0.0f,
                            .code_ok = true
                        };
                        send_event(&event);
                    }
                    break;

                case CMD_RESET_ALARM:
                    ESP_LOGI(TAG, "Received RESET_ALARM command");
                    if (safe_sm.current_state == STATE_ALARM) {
                        // Reset alarm to locked state
                        safe_sm.current_state = STATE_LOCKED;
                        safe_sm.wrong_count = 0;
                        set_locked_led();
                        notify_state_change(&safe_sm);
                    }
                    break;

                default:
                    ESP_LOGW(TAG, "Unknown command type: %d", cmd.type);
                    break;
            }
        }

        // Check accelerometer for movement (only when locked)
        if (safe_sm.current_state == STATE_LOCKED) {
            if (mpu6050_movement_detected()) {
                // Movement detected - trigger alarm
                float movement = mpu6050_read_movement();
                notify_movement(&safe_sm, movement);
                safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_MOVEMENT);
                if (new_state == STATE_ALARM) {
                    set_alarm_led_flashing();
                    notify_state_change(&safe_sm);
                }
            }
        }

        // Update LED flashing for alarm state
        leds_update();

        // Small delay to prevent busy-waiting
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}