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
