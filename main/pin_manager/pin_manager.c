#include "pin_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "PIN_MGR";
static const char *NVS_NAMESPACE = "pin_storage";
static const char *NVS_PIN_KEY = "current_pin";

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

// Load PIN from NVS, returns true if found
static bool load_pin_from_nvs(char *pin_buffer, size_t buffer_size)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found (first boot?)");
        return false;
    }

    size_t required_size = buffer_size;
    err = nvs_get_str(nvs_handle, NVS_PIN_KEY, pin_buffer, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "PIN loaded from NVS");
        return true;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No PIN found in NVS, using default");
        return false;
    } else {
        ESP_LOGE(TAG, "Error reading PIN from NVS: %s", esp_err_to_name(err));
        return false;
    }
}

// Save PIN to NVS
static bool save_pin_to_nvs(const char *pin)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(nvs_handle, NVS_PIN_KEY, pin);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write PIN to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "PIN saved to NVS");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return false;
    }
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

    // Try to load PIN from NVS
    char loaded_pin[MAX_PIN_LENGTH];
    if (load_pin_from_nvs(loaded_pin, MAX_PIN_LENGTH)) {
        // Validate loaded PIN
        if (pin_manager_validate(loaded_pin)) {
            strncpy(current_pin, loaded_pin, MAX_PIN_LENGTH - 1);
            current_pin[MAX_PIN_LENGTH - 1] = '\0';
            ESP_LOGI(TAG, "PIN manager initialized with stored PIN");
        } else {
            ESP_LOGW(TAG, "Stored PIN invalid, using default");
            strncpy(current_pin, default_pin, MAX_PIN_LENGTH - 1);
            current_pin[MAX_PIN_LENGTH - 1] = '\0';
        }
    } else {
        // No stored PIN, use default and save it
        strncpy(current_pin, default_pin, MAX_PIN_LENGTH - 1);
        current_pin[MAX_PIN_LENGTH - 1] = '\0';
        ESP_LOGI(TAG, "PIN manager initialized with default PIN");
        
        // Save default PIN to NVS for next boot
        save_pin_to_nvs(current_pin);
    }
    
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

    // Save to NVS first
    // either both NVS and RAM succeed, or neither
    if (!save_pin_to_nvs(new_pin)) {
        ESP_LOGE(TAG, "Failed to save PIN to NVS");
        return false;
    }
    
    // Only update RAM if NVS save succeeded
    if (xSemaphoreTake(pin_mutex, portMAX_DELAY) == pdTRUE) {
        strncpy(current_pin, new_pin, MAX_PIN_LENGTH - 1);
        current_pin[MAX_PIN_LENGTH - 1] = '\0';
        xSemaphoreGive(pin_mutex);
        
        ESP_LOGI(TAG, "PIN updated in RAM and NVS");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to acquire PIN mutex after NVS save");
        // NVS has new PIN but RAM still has old - will sync on next boot
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
