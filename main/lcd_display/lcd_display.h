#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../queue_manager/queue_manager.h"

// LCD controller I2C address (HD44780-compatible display)
#define LCD_CONTROLLER_ADDR 0x3E

// RGB backlight controller I2C address (separate chip)
#define LCD_BACKLIGHT_ADDR 0x60

// Initialize LCD display and I2C mutex
bool lcd_display_init(void);

// Get I2C mutex for shared bus protection (used by MPU6050)
SemaphoreHandle_t lcd_display_get_i2c_mutex(void);

// LCD display functions
void lcd_display_clear(void);
void lcd_display_write(const char *text, uint8_t row);
void lcd_display_set_backlight_rgb(uint8_t r, uint8_t g, uint8_t b);
void lcd_display_show_state(safe_state_t state);
void lcd_display_show_pin_entry(int length);
void lcd_display_clear_pin_entry(void);
void lcd_display_show_checking(void);
void lcd_display_show_message(const char *message, uint32_t duration_ms, safe_state_t state);

// FreeRTOS task that receives LCD commands and updates display
// Priority 2 - slow I2C writes, non-critical timing
void lcd_task(void *pvParameters);

#endif // LCD_DISPLAY_H
