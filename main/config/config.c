#include "config.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "CONFIG";

// Default values
static const char *correct_pin = "1234";

void config_init(void)
{
    FILE *fp = fopen("/spiffs/.env", "r");
    if (!fp) {
        ESP_LOGW(TAG, ".env not found, using defaults");
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        // Skip empty lines and comments
        if (line[0] == '\n' || line[0] == '#') {
            continue;
        }

        // Parse KEY=VALUE
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        // Remove trailing newline from value
        size_t value_len = strlen(value);
        if (value[value_len - 1] == '\n') {
            value[value_len - 1] = '\0';
        }

        if (strcmp(key, "CORRECT_PIN") == 0) {
            correct_pin = value;
            ESP_LOGI(TAG, "Loaded PIN from .env");
        }
    }

    fclose(fp);
    ESP_LOGI(TAG, "Config loaded successfully");
}

const char* config_get_correct_pin(void)
{
    return correct_pin;
}

