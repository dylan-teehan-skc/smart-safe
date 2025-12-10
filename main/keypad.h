#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

/**
 * @brief Initialize the 4x4 keypad matrix
 * 
 * Configures GPIO pins for rows (outputs) and columns (inputs with pull-ups).
 * Must be called before using keypad_get_key().
 */
void keypad_init(void);

/**
 * @brief Scan the keypad and return the pressed key
 * 
 * Performs a full scan of the 4x4 matrix with debouncing.
 * Returns the character of the pressed key, or '\0' if no key is pressed.
 * 
 * @return char The pressed key ('0'-'9', 'A'-'D', '*', '#') or '\0' if none
 */
char keypad_get_key(void);

/**
 * @brief Demo function to test keypad functionality
 * 
 * Continuously scans the keypad and prints detected keys to UART.
 * Useful for testing and verification.
 */
void keypad_demo(void);

#endif // KEYPAD_H