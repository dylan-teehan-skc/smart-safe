# Refactoring Plan for Smart Safe Control Task

## Goal
Reduce `main/control_task/control_task.c` from **379 lines** to **~150 lines** by extracting 3 modules while maintaining all functionality.

---

## Phase 1: Extract PIN Manager (Priority 1) ⭐⭐⭐
**Estimated time:** 1-2 hours  
**Impact:** Removes ~80 lines from `main/control_task/control_task.c`

### Steps:

#### 1.1 Create module structure
```bash
mkdir main/pin_manager
touch main/pin_manager/pin_manager.h
touch main/pin_manager/pin_manager.c
touch main/pin_manager/CMakeLists.txt
```

#### 1.2 Create `main/pin_manager/CMakeLists.txt`
```cmake
idf_component_register(
    SRCS "pin_manager.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_log
)
```

#### 1.3 Create `main/pin_manager/pin_manager.h`
```c
#ifndef PIN_MANAGER_H
#define PIN_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

#define PIN_LENGTH 4
#define MAX_PIN_LENGTH 8

/**
 * @brief Initialize PIN manager with default PIN
 * @param default_pin Initial PIN (must be PIN_LENGTH digits)
 * @return true on success, false on failure
 */
bool pin_manager_init(const char *default_pin);

/**
 * @brief Verify entered PIN against stored PIN (constant-time comparison)
 * @param entered_pin PIN to verify
 * @return true if PIN matches, false otherwise
 */
bool pin_manager_verify(const char *entered_pin);

/**
 * @brief Validate PIN format (length and digits)
 * @param pin PIN to validate
 * @return true if valid, false otherwise
 */
bool pin_manager_validate(const char *pin);

/**
 * @brief Set new PIN (validates and stores)
 * @param new_pin New PIN to set
 * @return true on success, false if invalid PIN
 */
bool pin_manager_set(const char *new_pin);

/**
 * @brief Cleanup PIN manager resources
 */
void pin_manager_cleanup(void);

#endif // PIN_MANAGER_H
```

#### 1.4 Create `main/pin_manager/pin_manager.c`
Move from `main/control_task/control_task.c`:
- Lines 20-22: `current_pin`, `pin_mutex`, `PIN_LENGTH`
- Lines 25-33: `control_task_init()` → rename to `pin_manager_init()`
- Lines 75-88: `constant_time_strcmp()`
- Lines 280-293: PIN validation logic from `CMD_SET_CODE`

New implementation:
```c
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
    while (a[i] != '\0' && b[i] != '\0') {
        diff |= a[i] ^ b[i];
        i++;
    }
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
```

#### 1.5 Update `main/control_task/CMakeLists.txt`
```cmake
idf_component_register(
    SRCS "control_task.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_log queue_manager state_machine led json_protocol mpu6050 keypad pin_manager
)
```

#### 1.6 Update `main/control_task/control_task.c`
**Remove:**
- Lines 20-22: PIN variables (moved to pin_manager)
- Lines 25-33: `control_task_init()` (moved to pin_manager)
- Lines 75-88: `constant_time_strcmp()` (moved to pin_manager)

**Replace in `process_pin_entry()`:**
```c
// OLD (lines 90-99):
char pin_copy[MAX_PIN_LENGTH];
if (xSemaphoreTake(pin_mutex, portMAX_DELAY) == pdTRUE) {
    strncpy(pin_copy, current_pin, MAX_PIN_LENGTH);
    xSemaphoreGive(pin_mutex);
} else {
    ESP_LOGE(TAG, "Failed to acquire PIN mutex");
    return;
}

if (constant_time_strcmp(pin, pin_copy) == 0) {

// NEW:
if (pin_manager_verify(pin)) {
```

**Replace in `CMD_SET_CODE`:**
```c
// OLD (lines 280-310):
{
    size_t code_len = strlen(cmd.code);
    bool valid = (code_len == PIN_LENGTH);
    
    // Check if all characters are digits
    if (valid) {
        for (size_t i = 0; i < code_len; i++) {
            if (cmd.code[i] < '0' || cmd.code[i] > '9') {
                valid = false;
                ESP_LOGW(TAG, "Invalid PIN: contains non-digit characters");
                break;
            }
        }
    } else {
        ESP_LOGW(TAG, "Invalid PIN: must be %d digits, got %zu", PIN_LENGTH, code_len);
    }
    
    if (valid) {
        if (xSemaphoreTake(pin_mutex, portMAX_DELAY) == pdTRUE) {
            strncpy(current_pin, cmd.code, MAX_PIN_LENGTH - 1);
            current_pin[MAX_PIN_LENGTH - 1] = '\0';
            xSemaphoreGive(pin_mutex);
            ESP_LOGI(TAG, "PIN code updated successfully");
        } else {
            ESP_LOGE(TAG, "Failed to acquire PIN mutex for update");
            valid = false;
        }
        ...
    }
}

// NEW:
bool valid = pin_manager_set(cmd.code);

if (valid) {
    // Send confirmation event
    event_t event = {
        .type = EVT_CODE_CHANGED,
        .timestamp = get_timestamp(),
        .state = safe_sm.current_state,
        .movement_amount = 0.0f,
        .code_ok = true
    };
    send_event(&event);
} else {
    // Send failure event
    event_t event = {
        .type = EVT_CODE_CHANGED,
        .timestamp = get_timestamp(),
        .state = safe_sm.current_state,
        .movement_amount = 0.0f,
        .code_ok = false
    };
    send_event(&event);
}
```

**Add to `control_task()`:**
```c
// After ESP_LOGI(TAG, "\nControl task started");
if (!pin_manager_init(CORRECT_PIN)) {
    ESP_LOGE(TAG, "Failed to initialize PIN manager");
    vTaskDelete(NULL);
    return;
}

// Remove lines 226-232 (old mutex creation)
```

**Add include at top:**
```c
#include "../pin_manager/pin_manager.h"
```

#### 1.7 Test Phase 1
```bash
idf.py build
idf.py flash monitor
```
**Test cases:**
- [ ] Enter correct PIN → should unlock
- [ ] Enter wrong PIN → should increment wrong count
- [ ] Send `set_code` command → should update PIN
- [ ] Enter new PIN → should work

---

## Phase 2: Extract Event Publisher (Priority 2) ⭐⭐
**Estimated time:** 30-45 minutes  
**Impact:** Removes ~60 lines of duplication

### Steps:

#### 2.1 Create module structure
```bash
mkdir main/event_publisher
touch main/event_publisher/event_publisher.h
touch main/event_publisher/event_publisher.c
touch main/event_publisher/CMakeLists.txt
```

#### 2.2 Create `main/event_publisher/CMakeLists.txt`
```cmake
idf_component_register(
    SRCS "event_publisher.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_log queue_manager state_machine
)
```

#### 2.3 Create `main/event_publisher/event_publisher.h`
```c
#ifndef EVENT_PUBLISHER_H
#define EVENT_PUBLISHER_H

#include "../state_machine/state_machine.h"

/**
 * @brief Publish state change event
 */
void publish_state_change(safe_state_machine_t *sm);

/**
 * @brief Publish movement detection event
 */
void publish_movement(safe_state_machine_t *sm, float movement_amount);

/**
 * @brief Publish PIN entry result event
 */
void publish_code_result(safe_state_machine_t *sm, bool code_ok);

/**
 * @brief Publish PIN code changed event
 */
void publish_code_changed(safe_state_machine_t *sm, bool success);

#endif // EVENT_PUBLISHER_H
```

#### 2.4 Create `main/event_publisher/event_publisher.c`
Consolidate all event creation logic:
```c
#include "event_publisher.h"
#include "../queue_manager/queue_manager.h"
#include "esp_timer.h"

static uint32_t get_timestamp(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

void publish_state_change(safe_state_machine_t *sm)
{
    event_t event = {
        .type = EVT_STATE_CHANGE,
        .timestamp = get_timestamp(),
        .state = sm->current_state,
        .movement_amount = 0.0f,
        .code_ok = false
    };
    send_event(&event);
}

void publish_movement(safe_state_machine_t *sm, float movement_amount)
{
    event_t event = {
        .type = EVT_MOVEMENT,
        .timestamp = get_timestamp(),
        .state = sm->current_state,
        .movement_amount = movement_amount,
        .code_ok = false
    };
    send_event(&event);
}

void publish_code_result(safe_state_machine_t *sm, bool code_ok)
{
    event_t event = {
        .type = EVT_CODE_RESULT,
        .timestamp = get_timestamp(),
        .state = sm->current_state,
        .movement_amount = 0.0f,
        .code_ok = code_ok
    };
    send_event(&event);
}

void publish_code_changed(safe_state_machine_t *sm, bool success)
{
    event_t event = {
        .type = EVT_CODE_CHANGED,
        .timestamp = get_timestamp(),
        .state = sm->current_state,
        .movement_amount = 0.0f,
        .code_ok = success
    };
    send_event(&event);
}
```

#### 2.5 Update `main/control_task/control_task.c`
**Remove:**
- Lines 25-29: `get_timestamp()` function
- Lines 31-43: `notify_state_change()` function
- Lines 45-57: `notify_movement()` function

**Replace all event creation with new functions:**
```c
// Replace ~9 instances of manual event creation with:
publish_state_change(&safe_sm);
publish_code_result(&safe_sm, true);
publish_movement(&safe_sm, movement);
publish_code_changed(&safe_sm, valid);
```

#### 2.6 Update `main/control_task/CMakeLists.txt`
```cmake
REQUIRES ... event_publisher
```

#### 2.7 Test Phase 2
```bash
idf.py build
idf.py flash monitor
```
**Test cases:**
- [ ] All events still published correctly
- [ ] Node-RED receives all telemetry

---

## Phase 3: Extract Command Handler (Priority 3) ⭐⭐
**Estimated time:** 1 hour  
**Impact:** Removes ~120 lines from `main/control_task/control_task.c`

### Steps:

#### 3.1 Create module structure
```bash
mkdir main/command_handler
touch main/command_handler/command_handler.h
touch main/command_handler/command_handler.c
touch main/command_handler/CMakeLists.txt
```

#### 3.2 Create `main/command_handler/CMakeLists.txt`
```cmake
idf_component_register(
    SRCS "command_handler.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_log queue_manager state_machine led pin_manager event_publisher
)
```

#### 3.3 Create `main/command_handler/command_handler.h`
```c
#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "../queue_manager/queue_manager.h"
#include "../state_machine/state_machine.h"

/**
 * @brief Process a command received from MQTT
 * @param cmd Command to process
 * @param sm State machine instance
 */
void command_handler_process(const command_t *cmd, safe_state_machine_t *sm);

#endif // COMMAND_HANDLER_H
```

#### 3.4 Create `main/command_handler/command_handler.c`
Move command switch statement (lines 260-354 from control_task.c):
```c
#include "command_handler.h"
#include "../pin_manager/pin_manager.h"
#include "../event_publisher/event_publisher.h"
#include "../led/leds.h"
#include "esp_log.h"

static const char *TAG = "CMD_HANDLER";

void command_handler_process(const command_t *cmd, safe_state_machine_t *sm)
{
    switch (cmd->type) {
        case CMD_LOCK:
            ESP_LOGI(TAG, "Received LOCK command");
            if (sm->current_state == STATE_UNLOCKED) {
                sm->current_state = STATE_LOCKED;
                set_locked_led();
                publish_state_change(sm);
            }
            break;

        case CMD_UNLOCK:
            ESP_LOGI(TAG, "Received UNLOCK command");
            if (sm->current_state == STATE_LOCKED) {
                state_machine_process_event(sm, EVENT_CORRECT_PIN);
                set_unlocked_led();
                publish_state_change(sm);
            }
            break;

        case CMD_SET_CODE:
            ESP_LOGI(TAG, "Received SET_CODE command");
            {
                bool valid = pin_manager_set(cmd->code);
                publish_code_changed(sm, valid);
            }
            break;

        case CMD_RESET_ALARM:
            ESP_LOGI(TAG, "Received RESET_ALARM command");
            if (sm->current_state == STATE_ALARM) {
                sm->current_state = STATE_LOCKED;
                sm->wrong_count = 0;
                set_locked_led();
                publish_state_change(sm);
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown command type: %d", cmd->type);
            break;
    }
}
```

#### 3.5 Update `main/control_task/control_task.c`
**Remove:**
- Lines 260-354: Entire command switch statement

**Replace with:**
```c
command_t cmd;
if (receive_command(&cmd, 0)) {
    command_handler_process(&cmd, &safe_sm);
}
```

#### 3.6 Update `main/control_task/CMakeLists.txt`
```cmake
REQUIRES ... command_handler
```

#### 3.7 Test Phase 3
```bash
idf.py build
idf.py flash monitor
```
**Test cases:**
- [ ] Lock command works
- [ ] Unlock command works
- [ ] Set code command works
- [ ] Reset alarm command works

---

## Final Result

### Before:
```
control_task.c: 379 lines
├── PIN management: 80 lines
├── Event creation: 60 lines
├── Command handling: 120 lines
└── Orchestration: 119 lines
```

### After:
```
control_task.c: 150 lines (orchestration only)
pin_manager.c: 100 lines
event_publisher.c: 80 lines
command_handler.c: 150 lines
```

---

## Testing Checklist

After each phase:
- [ ] Build succeeds
- [ ] Flash succeeds
- [ ] Correct PIN unlocks safe
- [ ] Wrong PIN increments counter
- [ ] 3 wrong PINs trigger alarm
- [ ] Movement triggers alarm
- [ ] Lock command works
- [ ] Unlock command works
- [ ] Set code command works
- [ ] Reset alarm command works
- [ ] Node-RED receives all events

---

## Commit Strategy

```bash
# Phase 1
git add main/pin_manager/
git add main/control_task/
git commit -m "refactor: extract PIN manager module

- Move PIN storage, validation, and verification to dedicated module
- Add mutex protection for thread-safe PIN access
- Reduce control_task.c by ~80 lines
- Improves testability and reusability"

# Phase 2
git add main/event_publisher/
git add main/control_task/
git commit -m "refactor: extract event publisher module

- Consolidate event creation into dedicated functions
- Eliminate code duplication (9 instances → 4 functions)
- Reduce control_task.c by ~60 lines"

# Phase 3
git add main/command_handler/
git add main/control_task/
git commit -m "refactor: extract command handler module

- Move command processing logic to dedicated module
- Improve separation of concerns
- Reduce control_task.c by ~120 lines"
```

---

## Priority Recommendation

If you only have time for **one refactoring**, do **Phase 1 (PIN Manager)**:
- Highest impact
- Most self-contained
- Best demonstrates software engineering principles
- Required for Phase 7 (NVS persistence)

---

## Notes

- All phases maintain 100% backward compatibility
- No functional changes, only structural improvements
- Each phase is independently testable
- Can be done incrementally over multiple sessions
