#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

/**
 * @brief Initialize the 4x4 keypad matrix with interrupt support
 * 
 * Configures GPIO pins for rows (outputs) and columns (inputs with pull-ups).
 * Enables falling edge interrupts on column pins for key press detection.
 * Must be called before using keypad_get_key() or keypad_wait_for_key().
 */
void keypad_init(void);

/**
 * @brief Non-blocking check for keypad press
 * 
 * Checks if a key press interrupt occurred and scans the matrix if so.
 * Returns immediately if no key is pressed.
 * 
 * @return char The pressed key (0-9, A-D, *, #) or '\0' if none
 */
char keypad_get_key(void);

/**
 * @brief Blocking wait for a key press
 * 
 * Waits for a key press interrupt, then scans and debounces the matrix.
 * CPU can sleep while waiting, making this power efficient.
 * 
 * @param timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return char The pressed key (0-9, A-D, *, #) or '\0' on timeout
 */
char keypad_wait_for_key(uint32_t timeout_ms);

/**
 * @brief Demo function to test keypad functionality
 * 
 * Continuously scans the keypad and prints detected keys to UART.
 * Useful for testing and verification.
 */
void keypad_demo(void);

#endif // KEYPAD_H