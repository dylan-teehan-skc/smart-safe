#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "../queue_manager/queue_manager.h"

// LCD controller I2C address (HD44780-compatible display)
#define LCD_CONTROLLER_ADDR 0x3E

// RGB backlight controller I2C address (separate chip)
#define LCD_BACKLIGHT_ADDR 0x60

/**
 * @brief Initialize the LCD display
 * @return true if successful, false otherwise
 */
bool lcd_display_init(void);

/**
 * @brief Clear the LCD display
 */
void lcd_display_clear(void);

/**
 * @brief Write text to a specific row
 * @param text String to display (max 16 characters)
 * @param row Row number (0 or 1)
 */
void lcd_display_write(const char *text, uint8_t row);

/**
 * @brief Set RGB backlight color
 * @param r Red intensity (0-255)
 * @param g Green intensity (0-255)
 * @param b Blue intensity (0-255)
 */
void lcd_display_set_backlight_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Display the current safe state with appropriate text and backlight color
 * @param state Current safe state (LOCKED, UNLOCKED, ALARM)
 */
void lcd_display_show_state(safe_state_t state);

/**
 * @brief Display PIN entry with masked asterisks
 * @param length Number of digits entered (0-4)
 */
void lcd_display_show_pin_entry(int length);

/**
 * @brief Clear PIN entry display
 */
void lcd_display_clear_pin_entry(void);

/**
 * @brief Show "Checking..." message
 */
void lcd_display_show_checking(void);

/**
 * @brief Display a temporary message that auto-clears after timeout
 * Uses timer interrupt for non-blocking operation
 * @param message Text to display on row 1 (max 16 characters)
 * @param duration_ms How long to display the message (milliseconds)
 * @param state Safe state to restore after timeout
 */
void lcd_display_show_message(const char *message, uint32_t duration_ms, safe_state_t state);

#endif // LCD_DISPLAY_H
