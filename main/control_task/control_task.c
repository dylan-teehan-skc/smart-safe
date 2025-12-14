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
#include "../pin_manager/pin_manager.h"
#include "../event_publisher/event_publisher.h"
#include "../command_handler/command_handler.h"
#include "../lcd_display/lcd_display.h"

static const char *TAG = "CTRL";

static safe_state_machine_t safe_sm;

// PIN entry buffer
static char pin_buffer[MAX_PIN_LENGTH] = {0};
static int pin_index = 0;

static void process_pin_entry(const char *pin)
{
    // Use pin_manager for constant-time verification
    if (pin_manager_verify(pin)) {
        ESP_LOGI(TAG, "Correct PIN entered");
        safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_CORRECT_PIN);
        ESP_LOGI(TAG, "State: %s", state_to_string(new_state));

        if (new_state == STATE_UNLOCKED) {
            set_unlocked_led();
        } else if (new_state == STATE_LOCKED) {
            set_locked_led();
        }
        
        event_publisher_code_result(&safe_sm, true);
        event_publisher_state_change(&safe_sm);
    } else {
        // Wrong PIN entered - only process if safe is locked
        if (safe_sm.current_state == STATE_LOCKED) {
            ESP_LOGW(TAG, "Wrong PIN entered");
            safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_WRONG_PIN);
            uint8_t wrong_count = state_machine_get_wrong_count(&safe_sm);
            ESP_LOGW(TAG, "Wrong attempts: %d/3", wrong_count);
            ESP_LOGI(TAG, "State: %s", state_to_string(new_state));

            if (new_state == STATE_ALARM) {
                set_alarm_led_flashing();
                event_publisher_state_change(&safe_sm);
            }
            
            event_publisher_code_result(&safe_sm, false);
        } else if (safe_sm.current_state == STATE_UNLOCKED) {
            ESP_LOGW(TAG, "Wrong PIN entered (safe already unlocked, ignoring)");
            event_publisher_code_result(&safe_sm, false);
        } else if (safe_sm.current_state == STATE_ALARM) {
            ESP_LOGW(TAG, "Wrong PIN entered (safe in alarm state, use correct PIN to reset)");
            event_publisher_code_result(&safe_sm, false);
        }
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
            
            // Update LCD with masked PIN entry
            lcd_display_show_pin_entry(pin_index);
        } else {
            ESP_LOGW(TAG, "PIN buffer full (%d digits max)", PIN_LENGTH);
        }
    }
    // Handle clear key (*)
    else if (key == '*') {
        clear_pin_buffer();
        lcd_display_clear_pin_entry();
    }
    // Handle submit key (#)
    else if (key == '#') {
        if (pin_index == PIN_LENGTH) {
            ESP_LOGI(TAG, "PIN submitted (%d digits)", PIN_LENGTH);
            lcd_display_show_checking();
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

    // Initialize PIN manager
    if (!pin_manager_init(CORRECT_PIN)) {
        ESP_LOGE(TAG, "Failed to initialize PIN manager");
        vTaskDelete(NULL);
        return;
    }

    // Initialize LEDs
    leds_init();
    set_locked_led();

    // Initialize accelerometer
    if (!mpu6050_init()) {
        ESP_LOGE(TAG, "Failed to initialize MPU6050 accelerometer");
    }

    // THREAD SAFETY: state machine is ONLY accessed from this control_task
    // Don't access from other tasks or ISRs without mutex protection
    safe_sm = state_machine_init();
    ESP_LOGI(TAG, "State machine initialized: %s", state_to_string(safe_sm.current_state));

    // Send initial state
    event_publisher_state_change(&safe_sm);
    
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
            command_handler_process(&cmd, &safe_sm);
        }

        // Check accelerometer for movement (only when locked)
        if (safe_sm.current_state == STATE_LOCKED) {
            if (mpu6050_movement_detected()) {
                // Movement detected - trigger alarm
                float movement = mpu6050_read_movement();
                event_publisher_movement(&safe_sm, movement);
                safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_MOVEMENT);
                if (new_state == STATE_ALARM) {
                    set_alarm_led_flashing();
                    event_publisher_state_change(&safe_sm);
                }
            }
        }

        // Update LED flashing for alarm state
        leds_update();

        // Small delay to prevent busy-waiting
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}