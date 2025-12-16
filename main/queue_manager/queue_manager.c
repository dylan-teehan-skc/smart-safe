#include "queue_manager.h"
#include "esp_log.h"

static const char *TAG = "QUEUE";

// Queue sizes
#define KEY_QUEUE_SIZE      10
#define SENSOR_QUEUE_SIZE   5
#define LED_QUEUE_SIZE      5
#define LCD_QUEUE_SIZE      5
#define EVENT_QUEUE_SIZE    10
#define CMD_QUEUE_SIZE      5

// Queue handles
QueueHandle_t key_queue = NULL;
QueueHandle_t sensor_queue = NULL;
QueueHandle_t led_queue = NULL;
QueueHandle_t lcd_queue = NULL;
QueueHandle_t event_queue = NULL;
QueueHandle_t cmd_queue = NULL;

// Helper to clean up queues on initialization failure
static void cleanup_queues(void)
{
    if (key_queue != NULL) { vQueueDelete(key_queue); key_queue = NULL; }
    if (sensor_queue != NULL) { vQueueDelete(sensor_queue); sensor_queue = NULL; }
    if (led_queue != NULL) { vQueueDelete(led_queue); led_queue = NULL; }
    if (lcd_queue != NULL) { vQueueDelete(lcd_queue); lcd_queue = NULL; }
    if (event_queue != NULL) { vQueueDelete(event_queue); event_queue = NULL; }
    if (cmd_queue != NULL) { vQueueDelete(cmd_queue); cmd_queue = NULL; }
}

bool queue_manager_init(void)
{
    // Key queue (keypad_task -> control_task)
    key_queue = xQueueCreate(KEY_QUEUE_SIZE, sizeof(key_event_t));
    if (key_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create key queue");
        cleanup_queues();
        return false;
    }
    ESP_LOGI(TAG, "Key queue created (size: %d)", KEY_QUEUE_SIZE);

    // Sensor queue (sensor_task -> control_task)
    sensor_queue = xQueueCreate(SENSOR_QUEUE_SIZE, sizeof(sensor_event_t));
    if (sensor_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor queue");
        cleanup_queues();
        return false;
    }
    ESP_LOGI(TAG, "Sensor queue created (size: %d)", SENSOR_QUEUE_SIZE);

    // LED queue (control_task -> led_task)
    led_queue = xQueueCreate(LED_QUEUE_SIZE, sizeof(led_cmd_t));
    if (led_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create LED queue");
        cleanup_queues();
        return false;
    }
    ESP_LOGI(TAG, "LED queue created (size: %d)", LED_QUEUE_SIZE);

    // LCD queue (control_task -> lcd_task)
    lcd_queue = xQueueCreate(LCD_QUEUE_SIZE, sizeof(lcd_cmd_t));
    if (lcd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create LCD queue");
        cleanup_queues();
        return false;
    }
    ESP_LOGI(TAG, "LCD queue created (size: %d)", LCD_QUEUE_SIZE);

    // Event queue (control_task -> comm_task)
    event_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(event_t));
    if (event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        cleanup_queues();
        return false;
    }
    ESP_LOGI(TAG, "Event queue created (size: %d)", EVENT_QUEUE_SIZE);

    // Command queue (comm_task -> control_task)
    cmd_queue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(command_t));
    if (cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create command queue");
        cleanup_queues();
        return false;
    }
    ESP_LOGI(TAG, "Command queue created (size: %d)", CMD_QUEUE_SIZE);

    return true;
}

// ============================================================================
// Keypad Queue
// ============================================================================

bool send_key_event(key_event_t *event)
{
    if (event == NULL || key_queue == NULL) return false;
    if (xQueueSend(key_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Key queue full");
        return false;
    }
    return true;
}

bool receive_key_event(key_event_t *event, uint32_t timeout_ms)
{
    if (event == NULL || key_queue == NULL) return false;
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(key_queue, event, ticks) == pdTRUE;
}

// ============================================================================
// Sensor Queue
// ============================================================================

bool send_sensor_event(sensor_event_t *event)
{
    if (event == NULL || sensor_queue == NULL) return false;
    if (xQueueSend(sensor_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Sensor queue full");
        return false;
    }
    return true;
}

bool receive_sensor_event(sensor_event_t *event, uint32_t timeout_ms)
{
    if (event == NULL || sensor_queue == NULL) return false;
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(sensor_queue, event, ticks) == pdTRUE;
}

// ============================================================================
// LED Queue
// ============================================================================

bool send_led_cmd(led_cmd_t *cmd)
{
    if (cmd == NULL || led_queue == NULL) return false;
    if (xQueueSend(led_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "LED queue full");
        return false;
    }
    return true;
}

bool receive_led_cmd(led_cmd_t *cmd, uint32_t timeout_ms)
{
    if (cmd == NULL || led_queue == NULL) return false;
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(led_queue, cmd, ticks) == pdTRUE;
}

// ============================================================================
// LCD Queue
// ============================================================================

bool send_lcd_cmd(lcd_cmd_t *cmd)
{
    if (cmd == NULL || lcd_queue == NULL) return false;
    if (xQueueSend(lcd_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "LCD queue full");
        return false;
    }
    return true;
}

bool receive_lcd_cmd(lcd_cmd_t *cmd, uint32_t timeout_ms)
{
    if (cmd == NULL || lcd_queue == NULL) return false;
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(lcd_queue, cmd, ticks) == pdTRUE;
}

// ============================================================================
// Event Queue (Telemetry)
// ============================================================================

bool send_event(event_t *event)
{
    if (event == NULL || event_queue == NULL) return false;
    if (xQueueSend(event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full");
        return false;
    }
    return true;
}

bool receive_event(event_t *event, uint32_t timeout_ms)
{
    if (event == NULL || event_queue == NULL) return false;
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(event_queue, event, ticks) == pdTRUE;
}

// ============================================================================
// Command Queue
// ============================================================================

bool send_command(command_t *cmd)
{
    if (cmd == NULL || cmd_queue == NULL) return false;
    if (xQueueSend(cmd_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full");
        return false;
    }
    return true;
}

bool receive_command(command_t *cmd, uint32_t timeout_ms)
{
    if (cmd == NULL || cmd_queue == NULL) return false;
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(cmd_queue, cmd, ticks) == pdTRUE;
}
