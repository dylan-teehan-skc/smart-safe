#include "command_handler.h"
#include "esp_log.h"
#include "../queue_manager/queue_manager.h"
#include "../pin_manager/pin_manager.h"
#include "../event_publisher/event_publisher.h"
#include "../mpu6050/mpu6050.h"

static const char *TAG = "CMD_HANDLER";

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
                sm->current_state = STATE_LOCKED;
                send_led_state(LED_CMD_LOCKED);
                send_lcd_state(STATE_LOCKED);
                event_publisher_state_change(sm);
            }
            break;

        case CMD_UNLOCK:
            ESP_LOGI(TAG, "Received UNLOCK command");
            if (sm->current_state == STATE_LOCKED) {
                sm->current_state = STATE_UNLOCKED;
                send_led_state(LED_CMD_UNLOCKED);
                send_lcd_state(STATE_UNLOCKED);
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
                sm->current_state = STATE_LOCKED;
                sm->wrong_count = 0;
                send_led_state(LED_CMD_LOCKED);
                send_lcd_state(STATE_LOCKED);
                event_publisher_state_change(sm);
            }
            break;

        case CMD_SET_SENSITIVITY:
            ESP_LOGI(TAG, "Received SET_SENSITIVITY command: %ld", (long)cmd->sensitivity);
            mpu6050_set_threshold(cmd->sensitivity);
            break;

        default:
            ESP_LOGW(TAG, "Unknown command type: %d", cmd->type);
            break;
    }
}
