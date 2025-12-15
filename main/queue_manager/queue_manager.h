#ifndef QUEUE_MANAGER_H
#define QUEUE_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>
#include <stdbool.h>

// Max PIN length (4-digit PIN + null terminator, with room for longer codes)
#define MAX_PIN_LENGTH 8

// ============================================================================
// Safe States (shared across tasks)
// ============================================================================

typedef enum {
    STATE_LOCKED,
    STATE_UNLOCKED,
    STATE_ALARM
} safe_state_t;

// ============================================================================
// Keypad Queue (keypad_task -> control_task)
// ============================================================================

typedef struct {
    char key;  // '0'-'9', '*', '#', 'A'-'D'
} key_event_t;

// ============================================================================
// Sensor Queue (sensor_task -> control_task)
// ============================================================================

typedef struct {
    float movement_g;  // Movement magnitude in g
} sensor_event_t;

// ============================================================================
// LED Queue (control_task -> led_task)
// ============================================================================

typedef enum {
    LED_CMD_LOCKED,
    LED_CMD_UNLOCKED,
    LED_CMD_ALARM
} led_cmd_type_t;

typedef struct {
    led_cmd_type_t type;
} led_cmd_t;

// ============================================================================
// LCD Queue (control_task -> lcd_task)
// ============================================================================

typedef enum {
    LCD_CMD_SHOW_STATE,
    LCD_CMD_SHOW_PIN_ENTRY,
    LCD_CMD_CLEAR_PIN_ENTRY,
    LCD_CMD_SHOW_MESSAGE,
    LCD_CMD_SHOW_CHECKING
} lcd_cmd_type_t;

typedef struct {
    lcd_cmd_type_t type;
    safe_state_t state;
    int pin_length;           // For LCD_CMD_SHOW_PIN_ENTRY
    char message[17];         // For LCD_CMD_SHOW_MESSAGE (max 16 chars + null)
    uint32_t duration_ms;     // For LCD_CMD_SHOW_MESSAGE
} lcd_cmd_t;

// ============================================================================
// Event Queue (control_task -> comm_task)
// ============================================================================

typedef enum {
    EVT_STATE_CHANGE,
    EVT_MOVEMENT,
    EVT_CODE_RESULT,
    EVT_CODE_CHANGED
} event_type_t;

typedef struct {
    event_type_t type;
    uint32_t timestamp;
    safe_state_t state;
    float movement_amount;
    bool code_ok;
} event_t;

// ============================================================================
// Command Queue (comm_task -> control_task)
// ============================================================================

typedef enum {
    CMD_LOCK,
    CMD_UNLOCK,
    CMD_SET_CODE,
    CMD_RESET_ALARM,
    CMD_SET_SENSITIVITY
} command_type_t;

typedef struct {
    command_type_t type;
    char code[MAX_PIN_LENGTH];
    int32_t sensitivity;  // For CMD_SET_SENSITIVITY (5000-50000)
} command_t;

// ============================================================================
// Queue Handles
// ============================================================================

extern QueueHandle_t key_queue;           // keypad_task -> control_task
extern QueueHandle_t sensor_queue;        // sensor_task -> control_task
extern QueueHandle_t led_queue;           // control_task -> led_task
extern QueueHandle_t lcd_queue;           // control_task -> lcd_task
extern QueueHandle_t event_queue;         // control_task -> comm_task
extern QueueHandle_t cmd_queue;           // comm_task -> control_task

// ============================================================================
// Queue Manager API
// ============================================================================

bool queue_manager_init(void);

// Keypad queue
bool send_key_event(key_event_t *event);
bool receive_key_event(key_event_t *event, uint32_t timeout_ms);

// Sensor queue
bool send_sensor_event(sensor_event_t *event);
bool receive_sensor_event(sensor_event_t *event, uint32_t timeout_ms);

// LED queue
bool send_led_cmd(led_cmd_t *cmd);
bool receive_led_cmd(led_cmd_t *cmd, uint32_t timeout_ms);

// LCD queue
bool send_lcd_cmd(lcd_cmd_t *cmd);
bool receive_lcd_cmd(lcd_cmd_t *cmd, uint32_t timeout_ms);

// Event queue (telemetry)
bool send_event(event_t *event);
bool receive_event(event_t *event, uint32_t timeout_ms);

// Command queue (remote commands)
bool send_command(command_t *cmd);
bool receive_command(command_t *cmd, uint32_t timeout_ms);

#endif
