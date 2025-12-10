#include "json_protocol.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "JSON";

const char* state_to_string(safe_state_t state)
{
    switch (state) {
        case STATE_LOCKED:   return "locked";
        case STATE_UNLOCKED: return "unlocked";
        case STATE_ALARM:    return "alarm";
        default:             return "unknown";
    }
}

int string_to_state(const char *str)
{
    if (strcmp(str, "locked") == 0)   return STATE_LOCKED;
    if (strcmp(str, "unlocked") == 0) return STATE_UNLOCKED;
    if (strcmp(str, "alarm") == 0)    return STATE_ALARM;
    return -1;
}

int event_to_json(const event_t *event, char *buffer, size_t buffer_size)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object for event serialization");
        return -1;
    }

    // Add timestamp and state
    cJSON_AddNumberToObject(root, "ts", event->timestamp);
    cJSON_AddStringToObject(root, "state", state_to_string(event->state));

    // Add event-specific fields
    switch (event->type) {
        case EVT_STATE_CHANGE:
            cJSON_AddStringToObject(root, "event", "state_change");
            break;

        case EVT_VIBRATION:
            cJSON_AddStringToObject(root, "event", "vibration");
            cJSON_AddBoolToObject(root, "vibration", event->vibration);
            break;

        case EVT_CODE_RESULT:
            cJSON_AddStringToObject(root, "event", "code_entry");
            cJSON_AddBoolToObject(root, "code_ok", event->code_ok);
            break;
    }

    // Print to buffer
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        cJSON_Delete(root);
        return -1;
    }

    int len = snprintf(buffer, buffer_size, "%s", json_str);

    free(json_str);
    cJSON_Delete(root);

    ESP_LOGD(TAG, "Event JSON: %s", buffer);
    return len;
}

bool json_to_command(const char *json, size_t len, command_t *cmd)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON command");
        return false;
    }

    // Get command field
    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(command)) {
        ESP_LOGE(TAG, "Missing or invalid 'command' field");
        cJSON_Delete(root);
        return false;
    }

    const char *cmd_str = command->valuestring;

    // Parse command type
    if (strcmp(cmd_str, "lock") == 0) {
        cmd->type = CMD_LOCK;
        cmd->code[0] = '\0';
    }
    else if (strcmp(cmd_str, "unlock") == 0) {
        cmd->type = CMD_UNLOCK;
        cmd->code[0] = '\0';
    }
    else if (strcmp(cmd_str, "set_code") == 0) {
        cmd->type = CMD_SET_CODE;

        cJSON *code = cJSON_GetObjectItem(root, "code");
        if (!cJSON_IsString(code)) {
            ESP_LOGE(TAG, "set_code requires 'code' field");
            cJSON_Delete(root);
            return false;
        }

        // Validate code length (max 7 characters)
        if (strlen(code->valuestring) > sizeof(cmd->code) - 1) {
            ESP_LOGE(TAG, "Code too long (max %d characters): '%s'", (int)(sizeof(cmd->code) - 1), code->valuestring);
            cJSON_Delete(root);
            return false;
        }
        strncpy(cmd->code, code->valuestring, sizeof(cmd->code) - 1);
        cmd->code[sizeof(cmd->code) - 1] = '\0';
    }
    else {
        ESP_LOGE(TAG, "Unknown command: %s", cmd_str);
        cJSON_Delete(root);
        return false;
    }

    ESP_LOGI(TAG, "Parsed command: %s", cmd_str);
    cJSON_Delete(root);
    return true;
}
