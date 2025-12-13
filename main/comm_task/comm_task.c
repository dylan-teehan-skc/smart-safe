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
static volatile bool mqtt_connected = false;

// Track initialization state
static bool netif_initialized = false;

// JSON buffer
#define JSON_BUFFER_SIZE 256

// Ring buffer for event buffering
#define EVENT_BUFFER_SIZE 10

typedef struct {
    event_t event;
    int msg_id;        // MQTT message ID for tracking delivery
    bool pending;      // true if published but not confirmed
    TickType_t timestamp; // Tick count when published (for timeout detection, wrap-safe)
} buffered_event_t;

typedef struct {
    buffered_event_t events[EVENT_BUFFER_SIZE];
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

static void buffer_event(const event_t *event, int msg_id, bool pending)
{
    if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        if (event_buffer.count >= EVENT_BUFFER_SIZE) {
            ESP_LOGW(TAG, "Buffer full, overwriting oldest event");
            // Advance tail to overwrite oldest event
            event_buffer.tail = (event_buffer.tail + 1) % EVENT_BUFFER_SIZE;
            event_buffer.count--;
        }

        // Copy event to buffer with tracking info
        memcpy(&event_buffer.events[event_buffer.head].event, event, sizeof(event_t));
        event_buffer.events[event_buffer.head].msg_id = msg_id;
        event_buffer.events[event_buffer.head].pending = pending;
        event_buffer.events[event_buffer.head].timestamp = xTaskGetTickCount();
        event_buffer.head = (event_buffer.head + 1) % EVENT_BUFFER_SIZE;
        event_buffer.count++;

        ESP_LOGI(TAG, "Event buffered (buffer: %d/%d, msg_id=%d, pending=%d)", 
                 event_buffer.count, EVENT_BUFFER_SIZE, msg_id, pending);
        
        xSemaphoreGive(event_buffer_mutex);
    }
}

static int find_next_non_pending_event(event_t *event)
{
    int found_index = -1;
    
    if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        // Find first non-pending event
        for (int i = 0; i < event_buffer.count; i++) {
            int index = (event_buffer.tail + i) % EVENT_BUFFER_SIZE;
            if (!event_buffer.events[index].pending) {
                // Copy event from buffer (but don't remove it yet)
                memcpy(event, &event_buffer.events[index].event, sizeof(event_t));
                found_index = index;
                break;
            }
        }
        
        xSemaphoreGive(event_buffer_mutex);
    }
    
    return found_index;
}

static void mark_event_pending(int index, int msg_id)
{
    if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        event_buffer.events[index].msg_id = msg_id;
        event_buffer.events[index].pending = true;
        event_buffer.events[index].timestamp = xTaskGetTickCount();
        ESP_LOGI(TAG, "Marked event as pending (index=%d, msg_id=%d, buffer: %d/%d)",
                 index, msg_id, event_buffer.count, EVENT_BUFFER_SIZE);
        xSemaphoreGive(event_buffer_mutex);
    }
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

static void mark_event_delivered(int msg_id)
{
    if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        // Search buffer for matching msg_id
        for (int i = 0; i < event_buffer.count; i++) {
            int index = (event_buffer.tail + i) % EVENT_BUFFER_SIZE;
            if (event_buffer.events[index].msg_id == msg_id && event_buffer.events[index].pending) {
                ESP_LOGI(TAG, "Marking event as delivered (msg_id=%d)", msg_id);
                
                // Optimize removal based on position
                if (i == 0) {
                    // Event is at tail - just advance tail pointer (O(1))
                    event_buffer.tail = (event_buffer.tail + 1) % EVENT_BUFFER_SIZE;
                } else if (i == event_buffer.count - 1) {
                    // Event is at head - just move head back (O(1))
                    event_buffer.head = (event_buffer.head - 1 + EVENT_BUFFER_SIZE) % EVENT_BUFFER_SIZE;
                } else {
                    // Event is in the middle - shift forward from tail to fill gap (O(i))
                    // This preserves FIFO order and only shifts elements before the removed one
                    for (int j = i; j > 0; j--) {
                        int src = (event_buffer.tail + j - 1) % EVENT_BUFFER_SIZE;
                        int dst = (event_buffer.tail + j) % EVENT_BUFFER_SIZE;
                        event_buffer.events[dst] = event_buffer.events[src];
                    }
                    event_buffer.tail = (event_buffer.tail + 1) % EVENT_BUFFER_SIZE;
                }
                
                event_buffer.count--;
                ESP_LOGI(TAG, "Event removed from buffer (remaining: %d)", event_buffer.count);
                break;
            }
        }
        
        xSemaphoreGive(event_buffer_mutex);
    }
}

static void check_pending_timeouts(void)
{
    TickType_t current_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(10000); // 10 seconds
    
    if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < event_buffer.count; i++) {
            int index = (event_buffer.tail + i) % EVENT_BUFFER_SIZE;
            buffered_event_t *buffered = &event_buffer.events[index];
            
            // Wrap-safe comparison: works correctly even when tick counter wraps around
            if (buffered->pending && (current_ticks - buffered->timestamp) >= timeout_ticks) {
                if (mqtt_connected && mqtt_client != NULL) {
                    // Copy event data while holding mutex
                    event_t event_copy;
                    memcpy(&event_copy, &buffered->event, sizeof(event_t));
                    int old_msg_id = buffered->msg_id;
                    
                    // Release mutex before MQTT publish (can block)
                    xSemaphoreGive(event_buffer_mutex);
                    
                    // Republish the event without holding mutex
                    char json_buffer[JSON_BUFFER_SIZE];
                    int len = event_to_json(&event_copy, json_buffer, JSON_BUFFER_SIZE);
                    int msg_id = -1;
                    
                    if (len > 0) {
                        msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_TELEMETRY, 
                                                        json_buffer, len, 1, 0);
                    }
                    
                    // Re-acquire mutex to update buffer state
                    if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
                        // Verify the event is still at the same index and still pending
                        if (i < event_buffer.count) {
                            buffered_event_t *recheck = &event_buffer.events[index];
                            if (recheck->pending && recheck->msg_id == old_msg_id) {
                                if (msg_id >= 0) {
                                    ESP_LOGW(TAG, "Republishing timed-out event (old_msg_id=%d, new_msg_id=%d)", 
                                            old_msg_id, msg_id);
                                    recheck->msg_id = msg_id;
                                    recheck->timestamp = current_ticks;
                                } else {
                                    ESP_LOGE(TAG, "Failed to republish timed-out event (msg_id=%d)", old_msg_id);
                                    recheck->pending = false; // Mark as not pending to retry later
                                }
                            }
                        }
                        // Continue loop - don't release mutex yet
                    } else {
                        return; // Failed to re-acquire mutex
                    }
                } else {
                    // Not connected, mark as not pending so it can be flushed when reconnected
                    ESP_LOGW(TAG, "Marking timed-out event as not pending (msg_id=%d)", buffered->msg_id);
                    buffered->pending = false;
                }
            }
        }
        
        xSemaphoreGive(event_buffer_mutex);
    }
}

static void flush_buffered_events(void)
{
    if (!has_buffered_events()) {
        return;
    }

    ESP_LOGI(TAG, "Flushing buffered events");

    event_t event;
    int event_index;
    while ((event_index = find_next_non_pending_event(&event)) >= 0) {
        // Check connection state before attempting publish
        // Read volatile variables once to ensure consistency
        bool is_connected = mqtt_connected;
        esp_mqtt_client_handle_t client = mqtt_client;
        
        if (!is_connected || client == NULL) {
            ESP_LOGW(TAG, "MQTT disconnected during flush, stopping flush");
            break;
        }
        
        char json_buffer[JSON_BUFFER_SIZE];
        int len = event_to_json(&event, json_buffer, JSON_BUFFER_SIZE);

        if (len > 0) {
            int msg_id = esp_mqtt_client_publish(client, MQTT_TOPIC_TELEMETRY, json_buffer, len, 1, 0);
            if (msg_id >= 0) {
                ESP_LOGI(TAG, "Queued buffered event (msg_id=%d)", msg_id);
                // Mark as pending to track delivery (event stays in buffer until confirmed)
                mark_event_pending(event_index, msg_id);
            } else {
                ESP_LOGE(TAG, "Failed to queue buffered event (error=%d), leaving in buffer", msg_id);
                break;
            }
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

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Message delivered to broker (msg_id=%d)", event->msg_id);
            mark_event_delivered(event->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error: %d", event->error_handle->esp_transport_sock_errno);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            }
            // Mark as disconnected to trigger buffering
            mqtt_connected = false;
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
        .network = {
            .timeout_ms = 5000,
        },
        .session = {
            .keepalive = 5,  // Detect disconnects in 5 seconds
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

    // buffer first to ensure zero data loss
    buffer_event(event, -1, false);

    // Then attempt to publish immediately if connected
    if (mqtt_connected && mqtt_client != NULL) {
        int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_TELEMETRY, json_buffer, len, 1, 0);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "Queued for MQTT (msg_id=%d)", msg_id);
            
            // Update the buffered event with msg_id and mark as pending
            if (xSemaphoreTake(event_buffer_mutex, portMAX_DELAY) == pdTRUE) {
                // Find the just-buffered event
                int last_index = (event_buffer.head - 1 + EVENT_BUFFER_SIZE) % EVENT_BUFFER_SIZE;
                event_buffer.events[last_index].msg_id = msg_id;
                event_buffer.events[last_index].pending = true;
                event_buffer.events[last_index].timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                xSemaphoreGive(event_buffer_mutex);
            }
        } else {
            ESP_LOGE(TAG, "MQTT publish failed (error=%d), event already buffered", msg_id);
        }
    } else {
        ESP_LOGW(TAG, "MQTT not connected, event buffered for later");
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

    uint32_t last_timeout_check = 0;
    const uint32_t timeout_check_interval = 2000; // Check every 2 seconds
    
    while (1) {
        event_t event;
        if (receive_event(&event, 1000)) {
            publish_telemetry(&event);
        }
        
        // Periodically check for timed-out pending events
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_time - last_timeout_check > timeout_check_interval) {
            check_pending_timeouts();
            last_timeout_check = current_time;
        }
    }

    // Cleanup on exit (if loop ever exits)
    comm_cleanup();
    vTaskDelete(NULL);
}
