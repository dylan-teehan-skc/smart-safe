#include "comm_task.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "../queue_manager/queue_manager.h"
#include "../json_protocol/json_protocol.h"
#include "../config.h"

static const char *TAG = "COMM";

// WiFi connection status
#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t wifi_event_group = NULL;

// MQTT client
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

// Track initialization state
static bool netif_initialized = false;

// JSON buffer
#define JSON_BUFFER_SIZE 256

// ============================================================================
// WiFi
// ============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
            mqtt_connected = false;
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return false;
    }

    // Only initialize netif and event loop once
    if (!netif_initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        netif_initialized = true;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register handlers
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    // Set credentials
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);

    // Wait for connection
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    return true;
}

// ============================================================================
// MQTT
// ============================================================================

static void mqtt_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_data == NULL) {
        ESP_LOGE(TAG, "MQTT event_data is NULL");
        return;
    }
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            mqtt_connected = true;
            int msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_COMMAND, 1);
            if (msg_id < 0) {
                ESP_LOGE(TAG, "Failed to subscribe to %s, error code: %d", MQTT_TOPIC_COMMAND, msg_id);
            } else {
                ESP_LOGI(TAG, "Subscribed: %s (msg_id=%d)", MQTT_TOPIC_COMMAND, msg_id);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            mqtt_connected = false;
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Command: %.*s", event->data_len, event->data);
            // Create null-terminated copy for safe JSON parsing
            if (event->data_len > 0 && event->data_len < JSON_BUFFER_SIZE) {
                char cmd_buffer[JSON_BUFFER_SIZE];
                memcpy(cmd_buffer, event->data, event->data_len);
                cmd_buffer[event->data_len] = '\0';
                handle_mqtt_command(cmd_buffer, event->data_len);
            } else {
                ESP_LOGW(TAG, "Command too large or empty");
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

static bool mqtt_init(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address = {
                .uri = MQTT_BROKER_URI,
            },
        },
    };

    mqtt_client = esp_mqtt_client_init(&cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return false;
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    ESP_LOGI(TAG, "MQTT connecting to %s", MQTT_BROKER_URI);
    return true;
}

// Cleanup function for resources
static void comm_cleanup(void)
{
    if (mqtt_client != NULL) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    mqtt_connected = false;

    if (wifi_event_group != NULL) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }
}

// ============================================================================
// Telemetry & Commands
// ============================================================================

static void publish_telemetry(event_t *event)
{
    char json_buffer[JSON_BUFFER_SIZE];
    int len = event_to_json(event, json_buffer, JSON_BUFFER_SIZE);

    if (len <= 0) {
        ESP_LOGE(TAG, "JSON conversion failed");
        return;
    }

    ESP_LOGI(TAG, "Telemetry: %s", json_buffer);

    if (mqtt_connected && mqtt_client != NULL) {
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_TELEMETRY, json_buffer, len, 1, 0);
    }
}

void handle_mqtt_command(const char *data, int len)
{
    command_t cmd;
    if (json_to_command(data, len, &cmd)) {
        send_command(&cmd);
    } else {
        ESP_LOGW(TAG, "Invalid command JSON");
    }
}

// ============================================================================
// Main Task
// ============================================================================

void comm_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Comm task started");

    if (!wifi_init()) {
        ESP_LOGE(TAG, "WiFi initialization failed");
        comm_cleanup();
        vTaskDelete(NULL);
        return;
    }

    if (!mqtt_init()) {
        ESP_LOGE(TAG, "MQTT initialization failed");
        comm_cleanup();
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        event_t event;
        if (receive_event(&event, 1000)) {
            publish_telemetry(&event);
        }
    }

    // Cleanup on exit (if loop ever exits)
    comm_cleanup();
    vTaskDelete(NULL);
}
