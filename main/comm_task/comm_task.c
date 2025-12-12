#include "comm_task.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
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

// Ring buffer for event buffering
#define EVENT_BUFFER_SIZE 10

typedef struct {
    event_t events[EVENT_BUFFER_SIZE];
    int head;   // Next write position
    int tail;   // Next read position
    int count;  // Number of events in buffer
} event_ring_buffer_t;

static event_ring_buffer_t event_buffer = {
    .head = 0,
    .tail = 0,
    .count = 0
};

// Mutex for thread-safe access to event buffer
static SemaphoreHandle_t event_buffer_mutex = NULL;

// ============================================================================
// Ring Buffer Functions
// ============================================================================

static void buffer_event(const event_t *event)
{
    if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        if (event_buffer.count >= EVENT_BUFFER_SIZE) {
            ESP_LOGW(TAG, "Buffer full, overwriting oldest event");
            // Advance tail to overwrite oldest event
            event_buffer.tail = (event_buffer.tail + 1) % EVENT_BUFFER_SIZE;
            event_buffer.count--;
        }

        // Copy event to buffer
        memcpy(&event_buffer.events[event_buffer.head], event, sizeof(event_t));
        event_buffer.head = (event_buffer.head + 1) % EVENT_BUFFER_SIZE;
        event_buffer.count++;

        ESP_LOGI(TAG, "Event buffered (buffer: %d/%d)", event_buffer.count, EVENT_BUFFER_SIZE);
        
        xSemaphoreGive(event_buffer_mutex);
    }
}

static bool get_buffered_event(event_t *event)
{
    bool result = false;
    
    if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        if (event_buffer.count > 0) {
            // Copy event from buffer
            memcpy(event, &event_buffer.events[event_buffer.tail], sizeof(event_t));
            event_buffer.tail = (event_buffer.tail + 1) % EVENT_BUFFER_SIZE;
            event_buffer.count--;
            result = true;
        }
        
        xSemaphoreGive(event_buffer_mutex);
    }
    
    return result;
}

static bool has_buffered_events(void)
{
    bool has_events = false;
    
    if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        has_events = (event_buffer.count > 0);
        xSemaphoreGive(event_buffer_mutex);
    }
    
    return has_events;
}

static void flush_buffered_events(void)
{
    if (!has_buffered_events()) {
        return;
    }

    ESP_LOGI(TAG, "Flushing buffered events");

    event_t event;
    while (get_buffered_event(&event)) {
        char json_buffer[JSON_BUFFER_SIZE];
        int len = event_to_json(&event, json_buffer, JSON_BUFFER_SIZE);

        if (len > 0 && mqtt_connected && mqtt_client != NULL) {
            int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_TELEMETRY, json_buffer, len, 1, 0);
            if (msg_id >= 0) {
                ESP_LOGI(TAG, "Published buffered event (%d remaining)", event_buffer.count);
            } else {
                ESP_LOGE(TAG, "Failed to publish buffered event (error=%d)", msg_id);
                // Re-buffer the event if publish failed
                buffer_event(&event);
                break;
            }
        } else {
            // Re-buffer the event if conditions changed
            buffer_event(&event);
            break;
        }

        // Small delay to avoid overwhelming the broker
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// WiFi
// ============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "WiFi station started, attempting connection...");
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "WiFi disconnected - Reason: %d", disconn->reason);
            
            // Log specific disconnect reasons
            switch (disconn->reason) {
                case WIFI_REASON_AUTH_EXPIRE:
                case WIFI_REASON_AUTH_LEAVE:
                case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                case WIFI_REASON_HANDSHAKE_TIMEOUT:
                    ESP_LOGE(TAG, "Authentication failed - check password");
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                    ESP_LOGE(TAG, "Access point not found - check SSID");
                    break;
                case WIFI_REASON_BEACON_TIMEOUT:
                    ESP_LOGE(TAG, "Beacon timeout - weak signal or AP disappeared");
                    break;
                default:
                    ESP_LOGW(TAG, "Other disconnect reason: %d", disconn->reason);
                    break;
            }
            
            ESP_LOGI(TAG, "Reconnecting...");
            mqtt_connected = false;
            esp_wifi_connect();
        } else {
            ESP_LOGD(TAG, "WiFi event: %ld", event_id);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_init(void)
{
    ESP_LOGI(TAG, "Starting WiFi initialization...");
    
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return false;
    }

    // Only initialize netif and event loop once
    if (!netif_initialized) {
        ESP_LOGI(TAG, "Initializing network interface...");
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        netif_initialized = true;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_LOGI(TAG, "WiFi driver initialized");

    // Register handlers
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    ESP_LOGI(TAG, "Event handlers registered");

    // Set credentials
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_LOGI(TAG, "Configuring WiFi credentials - SSID: %s, Password length: %d", 
             WIFI_SSID, strlen(WIFI_PASSWORD));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Disable power saving for more reliable connection
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "WiFi power saving disabled");

    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    ESP_LOGI(TAG, "Waiting for connection (this may take 10-30 seconds)...");

    // Wait for connection
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    
    ESP_LOGI(TAG, "WiFi initialization complete");
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
            // Flush any buffered events after successful connection
            flush_buffered_events();
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
    
    if (event_buffer_mutex != NULL) {
        vSemaphoreDelete(event_buffer_mutex);
        event_buffer_mutex = NULL;
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
        int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_TELEMETRY, json_buffer, len, 1, 0);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "Published to MQTT (msg_id=%d)", msg_id);
        } else {
            ESP_LOGE(TAG, "MQTT publish failed (error=%d)", msg_id);
        }
    } else {
        ESP_LOGW(TAG, "MQTT not connected, buffering event");
        buffer_event(event);
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

    // Create mutex for event buffer thread safety
    event_buffer_mutex = xSemaphoreCreateMutex();
    if (event_buffer_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create event buffer mutex");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Event buffer mutex created");

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
