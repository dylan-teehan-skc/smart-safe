#include "queue_manager.h"
#include "esp_log.h"

static const char *TAG = "QUEUE";

#define EVENT_QUEUE_SIZE   10
#define COMMAND_QUEUE_SIZE 5

// Queue handles
QueueHandle_t control_to_comm_queue = NULL;
QueueHandle_t comm_to_control_queue = NULL;

void queue_manager_init(void)
{
    // Create event queue (Control -> Comm)
    control_to_comm_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(event_t));
    if (control_to_comm_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
    } else {
        ESP_LOGI(TAG, "Event queue created (size: %d)", EVENT_QUEUE_SIZE);
    }

    // Create command queue (Comm -> Control)
    comm_to_control_queue = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(command_t));
    if (comm_to_control_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create command queue");
    } else {
        ESP_LOGI(TAG, "Command queue created (size: %d)", COMMAND_QUEUE_SIZE);
    }
}

bool send_event(event_t *event)
{
    if (control_to_comm_queue == NULL) {
        return false;
    }

    if (xQueueSend(control_to_comm_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event");
        return false;
    }

    ESP_LOGD(TAG, "Event sent: type=%d", event->type);
    return true;
}

bool receive_event(event_t *event, uint32_t timeout_ms)
{
    if (control_to_comm_queue == NULL) {
        return false;
    }

    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(control_to_comm_queue, event, ticks) == pdTRUE;
}

bool send_command(command_t *cmd)
{
    if (comm_to_control_queue == NULL) {
        return false;
    }

    if (xQueueSend(comm_to_control_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full, dropping command");
        return false;
    }

    ESP_LOGD(TAG, "Command sent: type=%d", cmd->type);
    return true;
}

bool receive_command(command_t *cmd)
{
    if (comm_to_control_queue == NULL) {
        return false;
    }
    if (cmd == NULL) {
        return false;
    }

    return xQueueReceive(comm_to_control_queue, cmd, 0) == pdTRUE;
}
