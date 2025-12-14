#include "pin_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "PIN_MGR";

static char current_pin[MAX_PIN_LENGTH] = {0};
static SemaphoreHandle_t pin_mutex = NULL;

// Constant-time string comparison to prevent timing attacks
static int constant_time_strcmp(const char *a, const char *b)
{
    int diff = 0;
    size_t i = 0;
    // Compare all characters, don't short-circuit on first difference
    while (a[i] != '\0' && b[i] != '\0') {
        diff |= a[i] ^ b[i];
        i++;
    }
    // Also check if lengths differ
    diff |= a[i] ^ b[i];
    return diff;
}

bool pin_manager_init(const char *default_pin)
{
    if (default_pin == NULL || !pin_manager_validate(default_pin)) {
        ESP_LOGE(TAG, "Invalid default PIN");
        return false;
    }

    pin_mutex = xSemaphoreCreateMutex();
    if (pin_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create PIN mutex");
        return false;
    }

    strncpy(current_pin, default_pin, MAX_PIN_LENGTH - 1);
    current_pin[MAX_PIN_LENGTH - 1] = '\0';
    
    ESP_LOGI(TAG, "PIN manager initialized");
    return true;
}

bool pin_manager_verify(const char *entered_pin)
{
    if (entered_pin == NULL) {
        return false;
    }

    char pin_copy[MAX_PIN_LENGTH];
    if (xSemaphoreTake(pin_mutex, portMAX_DELAY) == pdTRUE) {
        strncpy(pin_copy, current_pin, MAX_PIN_LENGTH);
        xSemaphoreGive(pin_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire PIN mutex");
        return false;
    }

    return (constant_time_strcmp(entered_pin, pin_copy) == 0);
}

bool pin_manager_validate(const char *pin)
{
    if (pin == NULL) {
        return false;
    }

    size_t len = strlen(pin);
    if (len != PIN_LENGTH) {
        ESP_LOGW(TAG, "Invalid PIN length: %zu (expected %d)", len, PIN_LENGTH);
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        if (pin[i] < '0' || pin[i] > '9') {
            ESP_LOGW(TAG, "Invalid PIN: contains non-digit characters");
            return false;
        }
    }

    return true;
}

bool pin_manager_set(const char *new_pin)
{
    if (!pin_manager_validate(new_pin)) {
        return false;
    }

    if (xSemaphoreTake(pin_mutex, portMAX_DELAY) == pdTRUE) {
        strncpy(current_pin, new_pin, MAX_PIN_LENGTH - 1);
        current_pin[MAX_PIN_LENGTH - 1] = '\0';
        xSemaphoreGive(pin_mutex);
        
        ESP_LOGI(TAG, "PIN updated successfully");
        // TODO: Store in NVS (Phase 7)
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to acquire PIN mutex");
        return false;
    }
}

void pin_manager_cleanup(void)
{
    if (pin_mutex != NULL) {
        vSemaphoreDelete(pin_mutex);
        pin_mutex = NULL;
    }
}
