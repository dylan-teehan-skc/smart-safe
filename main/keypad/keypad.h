#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

/**
 * @brief Initialize the 4x4 keypad matrix with interrupt support
 */
void keypad_init(void);

/**
 * @brief Non-blocking check for keypad press
 * @return char The pressed key (0-9, A-D, *, #) or '\0' if none
 */
char keypad_get_key(void);

/**
 * @brief FreeRTOS task that scans keypad and sends keys to key_queue
 * Priority 6 (highest) - user expects immediate response
 */
void keypad_task(void *pvParameters);

#endif // KEYPAD_H