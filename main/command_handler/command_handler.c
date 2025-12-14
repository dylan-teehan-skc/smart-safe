#include "command_handler.h"
#include "esp_log.h"
#include "../led/leds.h"
#include "../pin_manager/pin_manager.h"
#include "../event_publisher/event_publisher.h"

static const char *TAG = "CMD_HANDLER";

void command_handler_process(command_t *cmd, safe_state_machine_t *sm)
{
    if (cmd == NULL || sm == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return;
    }

    switch (cmd->type) {
        case CMD_LOCK:
            ESP_LOGI(TAG, "Received LOCK command");
            if (sm->current_state == STATE_UNLOCKED) {
                state_machine_process_event(sm, EVENT_CORRECT_PIN);
                sm->current_state = STATE_LOCKED;
                set_locked_led();
                event_publisher_state_change(sm);
            }
            break;

        case CMD_UNLOCK:
            ESP_LOGI(TAG, "Received UNLOCK command");
            if (sm->current_state == STATE_LOCKED) {
                sm->current_state = STATE_UNLOCKED;
                set_unlocked_led();
                event_publisher_state_change(sm);
            }
            break;

        case CMD_SET_CODE:
            ESP_LOGI(TAG, "Received SET_CODE command");
            {
                bool success = pin_manager_set(cmd->code);
                event_publisher_code_changed(sm, success);
            }
            break;

        case CMD_RESET_ALARM:
            ESP_LOGI(TAG, "Received RESET_ALARM command");
            if (sm->current_state == STATE_ALARM) {
                // Reset alarm to locked state
                sm->current_state = STATE_LOCKED;
                sm->wrong_count = 0;
                set_locked_led();
                event_publisher_state_change(sm);
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown command type: %d", cmd->type);
            break;
    }
}
