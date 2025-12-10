#ifndef JSON_PROTOCOL_H
#define JSON_PROTOCOL_H

/* 
 * JSON Protocol Specification
 *
 * MQTT Topics:
 *   Telemetry: smartsafe/<device_id>/telemetry  (ESP32 -> Broker -> Node-RED)
 *   Commands:  smartsafe/<device_id>/command    (Node-RED -> Broker -> ESP32)
 *
 * -------------------------------------------------------------------------
 * TELEMETRY MESSAGES (published by ESP32)
 * -------------------------------------------------------------------------
 *
 * State Change Event:
 *   {"ts":1234567890,"state":"locked","event":"state_change"}
 *   {"ts":1234567890,"state":"unlocked","event":"state_change"}
 *   {"ts":1234567890,"state":"alarm","event":"state_change"}
 *
 * Vibration Event:
 *   {"ts":1234567890,"state":"locked","event":"vibration","vibration":true}
 *
 * Code Entry Event:
 *   {"ts":1234567890,"state":"locked","event":"code_entry","code_ok":true}
 *   {"ts":1234567890,"state":"locked","event":"code_entry","code_ok":false}
 *
 * Fields:
 *   ts        - Unix timestamp (seconds since epoch)
 *   state     - Current safe state: "locked", "unlocked", "alarm"
 *   event     - Event type: "state_change", "vibration", "code_entry"
 *   vibration - Boolean, present only for vibration events
 *   code_ok   - Boolean, present only for code entry events
 *
 * -------------------------------------------------------------------------
 * COMMAND MESSAGES (received by ESP32)
 * -------------------------------------------------------------------------
 *
 * Lock Command:
 *   {"command":"lock"}
 *
 * Unlock Command:
 *   {"command":"unlock"}
 *
 * Set Code Command:
 *   {"command":"set_code","code":"1234"}
 *
 * Fields:
 *   command - Command type: "lock", "unlock", "set_code"
 *   code    - New PIN code (required only for set_code command)
 */

#include "../queue_manager/queue_manager.h"
#include <stdbool.h>

// Convert event to JSON string for MQTT publishing
int event_to_json(const event_t *event, char *buffer, size_t buffer_size);

// Parse JSON command string into command struct
bool json_to_command(const char *json, size_t len, command_t *cmd);

// Convert safe_state_t to string ("locked", "unlocked", "alarm")
const char* state_to_string(safe_state_t state);

// Convert string to safe_state_t (-1 if invalid)
int string_to_state(const char *str);

#endif
