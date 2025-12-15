#include "event_publisher.h"
#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "../queue_manager/queue_manager.h"
#include "../json_protocol/json_protocol.h"
#include "../lcd_display/lcd_display.h"

static const char *TAG = "EVT_PUB";

void event_publisher_init(void)
{
    ESP_LOGI(TAG, "Event publisher initialized");
}

// Get current timestamp in seconds
static uint32_t get_timestamp(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

void event_publisher_state_change(safe_state_machine_t *sm)
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
    
    // Update LCD display with new state
    lcd_display_show_state(sm->current_state);
}

void event_publisher_movement(safe_state_machine_t *sm, float movement)
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

void event_publisher_code_result(safe_state_machine_t *sm, bool correct)
{
    event_t event = {
        .type = EVT_CODE_RESULT,
        .timestamp = get_timestamp(),
        .state = sm->current_state,
        .movement_amount = 0.0f,
        .code_ok = correct
    };
    send_event(&event);
}

void event_publisher_code_changed(safe_state_machine_t *sm, bool success)
{
    event_t event = {
        .type = EVT_CODE_CHANGED,
        .timestamp = get_timestamp(),
        .state = sm->current_state,
        .movement_amount = 0.0f,
        .code_ok = success
    };
    send_event(&event);
}
