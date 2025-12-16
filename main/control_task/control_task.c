#include "control_task.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "../queue_manager/queue_manager.h"
#include "../state_machine/state_machine.h"
#include "../json_protocol/json_protocol.h"
#include "../config.h"
#include "../pin_manager/pin_manager.h"
#include "../event_publisher/event_publisher.h"
#include "../command_handler/command_handler.h"

static const char *TAG = "CTRL";

static safe_state_machine_t safe_sm;

// PIN entry buffer
static char pin_buffer[MAX_PIN_LENGTH] = {0};
static int pin_index = 0;

// Helper to send LED command
static void send_led_state(led_cmd_type_t type)
{
    led_cmd_t cmd = { .type = type };
    send_led_cmd(&cmd);
}

// Helper to send LCD state
static void send_lcd_state(safe_state_t state)
{
    lcd_cmd_t cmd = {
        .type = LCD_CMD_SHOW_STATE,
        .state = state
    };
    send_lcd_cmd(&cmd);
}

// Helper to send LCD PIN entry
static void send_lcd_pin_entry(int length)
{
    lcd_cmd_t cmd = {
        .type = LCD_CMD_SHOW_PIN_ENTRY,
        .pin_length = length
    };
    send_lcd_cmd(&cmd);
}

// Helper to send LCD message
static void send_lcd_message(const char *message, uint32_t duration_ms, safe_state_t state)
{
    lcd_cmd_t cmd = {
        .type = LCD_CMD_SHOW_MESSAGE,
        .state = state,
        .duration_ms = duration_ms
    };
    strncpy(cmd.message, message, sizeof(cmd.message) - 1);
    cmd.message[sizeof(cmd.message) - 1] = '\0';
    send_lcd_cmd(&cmd);
}

// Helper to send LCD clear PIN entry
static void send_lcd_clear_pin(void)
{
    lcd_cmd_t cmd = { .type = LCD_CMD_CLEAR_PIN_ENTRY };
    send_lcd_cmd(&cmd);
}

// Helper to send LCD checking
static void send_lcd_checking(void)
{
    lcd_cmd_t cmd = { .type = LCD_CMD_SHOW_CHECKING };
    send_lcd_cmd(&cmd);
}

static void process_pin_entry(const char *pin)
{
    if (pin_manager_verify(pin)) {
        ESP_LOGI(TAG, "Correct PIN entered");
        safe_state_t old_state = safe_sm.current_state;
        safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_CORRECT_PIN);
        ESP_LOGI(TAG, "State: %s", state_to_string(new_state));

        if (new_state == STATE_UNLOCKED) {
            send_led_state(LED_CMD_UNLOCKED);
            send_lcd_state(new_state);
        } else if (new_state == STATE_LOCKED) {
            send_led_state(LED_CMD_LOCKED);
            send_lcd_state(new_state);
        }

        event_publisher_code_result(&safe_sm, true);
        // Only publish state change if state actually changed
        if (new_state != old_state) {
            event_publisher_state_change(&safe_sm);
        }
    } else {
        if (safe_sm.current_state == STATE_LOCKED) {
            ESP_LOGW(TAG, "Wrong PIN entered");
            safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_WRONG_PIN);
            uint8_t wrong_count = state_machine_get_wrong_count(&safe_sm);
            ESP_LOGW(TAG, "Wrong attempts: %d/3", wrong_count);

            if (new_state == STATE_ALARM) {
                send_led_state(LED_CMD_ALARM);
                send_lcd_message("ALARM!", 2000, new_state);
                event_publisher_state_change(&safe_sm);
            } else {
                char msg[17];
                snprintf(msg, sizeof(msg), "Wrong! %d/3", wrong_count);
                send_lcd_message(msg, 2000, new_state);
            }

            event_publisher_code_result(&safe_sm, false);
        } else if (safe_sm.current_state == STATE_UNLOCKED) {
            send_lcd_message("Already Open", 2000, safe_sm.current_state);
            event_publisher_code_result(&safe_sm, false);
        } else if (safe_sm.current_state == STATE_ALARM) {
            send_lcd_message("Use Correct PIN", 2000, safe_sm.current_state);
            event_publisher_code_result(&safe_sm, false);
        }
    }
}

static void clear_pin_buffer(void)
{
    memset(pin_buffer, 0, sizeof(pin_buffer));
    pin_index = 0;
}

static void handle_key_press(char key)
{
    if (key >= '0' && key <= '9') {
        if (pin_index < PIN_LENGTH) {
            pin_buffer[pin_index++] = key;
            pin_buffer[pin_index] = '\0';
            ESP_LOGI(TAG, "PIN entry: %d digits", pin_index);
            send_lcd_pin_entry(pin_index);
        }
    }
    else if (key == '*') {
        clear_pin_buffer();
        send_lcd_clear_pin();
    }
    else if (key == '#') {
        if (pin_index == PIN_LENGTH) {
            send_lcd_checking();
            process_pin_entry(pin_buffer);
        }
        clear_pin_buffer();
    }
}

static void handle_movement(float movement_g)
{
    if (safe_sm.current_state == STATE_LOCKED) {
        event_publisher_movement(&safe_sm, movement_g);
        safe_state_t new_state = state_machine_process_event(&safe_sm, EVENT_MOVEMENT);
        if (new_state == STATE_ALARM) {
            send_led_state(LED_CMD_ALARM);
            send_lcd_state(STATE_ALARM);
            event_publisher_state_change(&safe_sm);
        }
    }
}

void control_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Control task started (Priority 4)");

    // Initialize PIN manager
    if (!pin_manager_init(CORRECT_PIN)) {
        ESP_LOGE(TAG, "Failed to initialize PIN manager");
        vTaskDelete(NULL);
        return;
    }

    // Initialize state machine
    safe_sm = state_machine_init();
    ESP_LOGI(TAG, "State machine initialized: %s", state_to_string(safe_sm.current_state));

    // Send initial state to LED and LCD tasks
    send_led_state(LED_CMD_LOCKED);
    send_lcd_state(STATE_LOCKED);

    // Publish initial state
    event_publisher_state_change(&safe_sm);

    // Register with watchdog
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "Control task registered with watchdog");

    ESP_LOGI(TAG, "Ready for input");

    while (1) {
        // Feed the watchdog
        esp_task_wdt_reset();

        // Check for key events (non-blocking)
        key_event_t key_evt;
        if (receive_key_event(&key_evt, 0)) {
            handle_key_press(key_evt.key);
        }

        // Check for sensor events (non-blocking)
        sensor_event_t sensor_evt;
        if (receive_sensor_event(&sensor_evt, 0)) {
            handle_movement(sensor_evt.movement_g);
        }

        // Check for remote commands (non-blocking)
        command_t cmd;
        if (receive_command(&cmd, 0)) {
            command_handler_process(&cmd, &safe_sm);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
