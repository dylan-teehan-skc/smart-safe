#include "comm_task.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "../queue_manager/queue_manager.h"
#include "../json_protocol/json_protocol.h"

static const char *TAG = "COMM";

// JSON buffer for telemetry
#define JSON_BUFFER_SIZE 256
static char json_buffer[JSON_BUFFER_SIZE];

// Publish telemetry (placeholder - will add MQTT later)
static void publish_telemetry(event_t *event)
{
    int len = event_to_json(event, json_buffer, JSON_BUFFER_SIZE);
    if (len > 0) {
        ESP_LOGI(TAG, "Telemetry: %s", json_buffer);
        // TODO: mqtt_publish(telemetry_topic, json_buffer, len);
    }
}

// Handle incoming MQTT message (placeholder - will be called by MQTT callback)
void handle_mqtt_command(const char *data, int len)
{
    command_t cmd;
    if (json_to_command(data, len, &cmd)) {
        send_command(&cmd);
    }
}

void comm_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Comm task started");

    // TODO: wifi_init();
    // TODO: mqtt_init();

    while (1) {
        // Wait for events from control task (blocks up to 1000ms)
        event_t event;
        if (receive_event(&event, 1000)) {
            publish_telemetry(&event);
        }

        // TODO: Handle WiFi reconnection
        // TODO: Handle MQTT reconnection
    }
}
