#include "lcd_display.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "LCD";

// Timer handle for message timeout
static esp_timer_handle_t message_timer = NULL;
static safe_state_t restore_state = STATE_LOCKED;
static volatile bool message_active = false;

// I2C port (shared with MPU6050)
#define I2C_PORT I2C_NUM_0

// Mutex for I2C bus protection (shared with MPU6050)
// IMPORTANT: All I2C transactions to LCD and MPU6050 must acquire this mutex
// to prevent bus contention and transaction interleaving
static SemaphoreHandle_t i2c_mutex = NULL;

// RGB backlight registers for DFRobot DFR0464 (address 0x60)
#define RGB_MODE1               0x00
#define RGB_MODE2               0x01
#define RGB_PWM_BLUE            0x02  // Note: DFRobot uses 0x02 for BLUE
#define RGB_PWM_GREEN           0x03
#define RGB_PWM_RED             0x04  // Note: DFRobot uses 0x04 for RED
#define RGB_LEDOUT              0x08

// LCD commands
#define LCD_CMD_CLEAR           0x01
#define LCD_CMD_HOME            0x02
#define LCD_CMD_ENTRY_MODE      0x04
#define LCD_CMD_DISPLAY_CTRL    0x08
#define LCD_CMD_FUNCTION_SET    0x20
#define LCD_CMD_SET_DDRAM_ADDR  0x80

// Entry mode flags
#define LCD_ENTRY_LEFT          0x02
#define LCD_ENTRY_SHIFT_DEC     0x00

// Display control flags
#define LCD_DISPLAY_ON          0x04
#define LCD_CURSOR_OFF          0x00
#define LCD_BLINK_OFF           0x00

// Function set flags
#define LCD_FUNCTION_4BIT       0x00
#define LCD_FUNCTION_2LINE      0x08
#define LCD_FUNCTION_5x8        0x00

// Control bits (for controller at 0x3E)
#define LCD_EN                  0x04  // Enable bit
#define LCD_RW                  0x02  // Read/Write bit
#define LCD_RS                  0x01  // Register select bit

// Send command to LCD controller (0x3E) using register protocol
static esp_err_t lcd_send_command(uint8_t cmd)
{
    if (i2c_mutex == NULL || xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire I2C mutex for command");
        return ESP_ERR_TIMEOUT;
    }
    
    i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();
    i2c_master_start(i2c_cmd);
    i2c_master_write_byte(i2c_cmd, (LCD_CONTROLLER_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(i2c_cmd, 0x00, true);  // Control byte: Co=0, RS=0 (command)
    i2c_master_write_byte(i2c_cmd, cmd, true);
    i2c_master_stop(i2c_cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, i2c_cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(i2c_cmd);
    
    xSemaphoreGive(i2c_mutex);
    return ret;
}

// Send data to LCD controller (0x3E) using register protocol
static esp_err_t lcd_send_data_byte(uint8_t data)
{
    if (i2c_mutex == NULL || xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire I2C mutex for data");
        return ESP_ERR_TIMEOUT;
    }
    
    i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();
    i2c_master_start(i2c_cmd);
    i2c_master_write_byte(i2c_cmd, (LCD_CONTROLLER_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(i2c_cmd, 0x40, true);  // Control byte: Co=0, RS=1 (data)
    i2c_master_write_byte(i2c_cmd, data, true);
    i2c_master_stop(i2c_cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, i2c_cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(i2c_cmd);
    
    xSemaphoreGive(i2c_mutex);
    return ret;
}

// Write to RGB backlight controller (0x60)
static esp_err_t rgb_write_register(uint8_t reg, uint8_t value)
{
    if (i2c_mutex == NULL || xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire I2C mutex for RGB");
        return ESP_ERR_TIMEOUT;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LCD_BACKLIGHT_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    xSemaphoreGive(i2c_mutex);
    return ret;
}

SemaphoreHandle_t lcd_display_get_i2c_mutex(void)
{
    return i2c_mutex;
}

bool lcd_display_init(void)
{
    ESP_LOGI(TAG, "Initializing LCD controller at 0x%02X", LCD_CONTROLLER_ADDR);
    ESP_LOGI(TAG, "Initializing RGB backlight at 0x%02X", LCD_BACKLIGHT_ADDR);
    
    // Create I2C mutex for bus protection (shared with MPU6050)
    if (i2c_mutex == NULL) {
        i2c_mutex = xSemaphoreCreateMutex();
        if (i2c_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create I2C mutex");
            return false;
        }
    }
    
    // Wait for LCD to power up
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Initialize RGB backlight controller (DFRobot DFR0464 at 0x60)
    esp_err_t ret = rgb_write_register(RGB_MODE1, 0x00);    // Normal mode
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure RGB MODE1: %s", esp_err_to_name(ret));
        return false;
    }
    ret = rgb_write_register(RGB_LEDOUT, 0xFF);   // Enable all LED outputs (full PWM)
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure RGB LEDOUT: %s", esp_err_to_name(ret));
        return false;
    }
    ret = rgb_write_register(RGB_MODE2, 0x20);    // DMBLNK to 1 (blink mode)
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure RGB MODE2: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "RGB backlight controller initialized");
    
    // LCD initialization sequence for HD44780 via I2C
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Function set: 8-bit mode, 2 lines, 5x8 font (DFRobot uses 8-bit I2C interface)
    ret = lcd_send_command(LCD_CMD_FUNCTION_SET | 0x10 | LCD_FUNCTION_2LINE | LCD_FUNCTION_5x8);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send function set command: %s", esp_err_to_name(ret));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    
    // Display control: display on, cursor off, blink off
    ret = lcd_send_command(LCD_CMD_DISPLAY_CTRL | LCD_DISPLAY_ON | LCD_CURSOR_OFF | LCD_BLINK_OFF);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send display control command: %s", esp_err_to_name(ret));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // Clear display
    ret = lcd_send_command(LCD_CMD_CLEAR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear display: %s", esp_err_to_name(ret));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    
    // Entry mode: left to right, no shift
    ret = lcd_send_command(LCD_CMD_ENTRY_MODE | LCD_ENTRY_LEFT | LCD_ENTRY_SHIFT_DEC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send entry mode command: %s", esp_err_to_name(ret));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
    
    ESP_LOGI(TAG, "LCD initialized successfully");
    return true;
}

void lcd_display_clear(void)
{
    esp_err_t ret = lcd_send_command(LCD_CMD_CLEAR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear display: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd_display_write(const char *text, uint8_t row)
{
    if (row > 1) {
        ESP_LOGW(TAG, "Invalid row: %d (must be 0 or 1)", row);
        return;
    }
    
    // Set cursor position (row 0 = 0x00, row 1 = 0x40)
    uint8_t row_addr = (row == 0) ? 0x00 : 0x40;
    esp_err_t ret = lcd_send_command(LCD_CMD_SET_DDRAM_ADDR | row_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set cursor position for row %d: %s", row, esp_err_to_name(ret));
        return;
    }
    
    // Longer delay after cursor positioning to ensure it's processed
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Write up to 16 characters with small delays between each
    for (int i = 0; i < 16; i++) {
        if (text[i] == '\0') {
            // Pad remaining space with spaces
            for (int j = i; j < 16; j++) {
                ret = lcd_send_data_byte(' ');
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to write padding at position %d: %s", j, esp_err_to_name(ret));
                }
                vTaskDelay(pdMS_TO_TICKS(1));  // Small delay between characters
            }
            break;
        }
        ret = lcd_send_data_byte(text[i]);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to write character '%c' at position %d: %s", text[i], i, esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(1));  // Small delay between characters
    }
}

void lcd_display_set_backlight_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_LOGI(TAG, "Setting RGB backlight: R=%d G=%d B=%d", r, g, b);
    
    esp_err_t ret = rgb_write_register(RGB_PWM_RED, r);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set red PWM: %s", esp_err_to_name(ret));
    }
    ret = rgb_write_register(RGB_PWM_GREEN, g);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set green PWM: %s", esp_err_to_name(ret));
    }
    ret = rgb_write_register(RGB_PWM_BLUE, b);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set blue PWM: %s", esp_err_to_name(ret));
    }
}

void lcd_display_show_state(safe_state_t state)
{
    // Cancel any active message timer to prevent it from overwriting this state
    if (message_timer != NULL) {
        esp_timer_handle_t timer_to_delete = message_timer;
        message_timer = NULL;
        message_active = false;
        esp_timer_stop(timer_to_delete);
        esp_timer_delete(timer_to_delete);
    }

    lcd_display_clear();

    switch (state) {
        case STATE_LOCKED:
            lcd_display_write("Status: LOCKED", 0);
            lcd_display_write("Ready", 1);
            lcd_display_set_backlight_rgb(255, 0, 0);  // Red
            ESP_LOGI(TAG, "Displaying LOCKED state");
            break;
            
        case STATE_UNLOCKED:
            lcd_display_write("Status: UNLOCKED", 0);
            lcd_display_write("Access Granted", 1);
            lcd_display_set_backlight_rgb(0, 255, 0);  // Green
            ESP_LOGI(TAG, "Displaying UNLOCKED state");
            break;
            
        case STATE_ALARM:
            lcd_display_write("!! ALARM !!", 0);
            lcd_display_write("Tamper Detected", 1);
            lcd_display_set_backlight_rgb(255, 0, 0);  // Red
            ESP_LOGI(TAG, "Displaying ALARM state");
            break;
            
        default:
            lcd_display_write("Status: UNKNOWN", 0);
            lcd_display_set_backlight_rgb(255, 255, 0);  // Yellow
            ESP_LOGE(TAG, "Unknown state: %d", state);
            break;
    }
}

void lcd_display_show_pin_entry(int length)
{
    // Cancel any active message timer when user starts entering PIN
    if (message_timer != NULL) {
        esp_timer_handle_t timer_to_delete = message_timer;
        message_timer = NULL;  // Clear handle first to prevent callback from accessing
        message_active = false;
        
        // Stop and delete timer (ignore errors if already fired)
        esp_timer_stop(timer_to_delete);
        esp_timer_delete(timer_to_delete);
    }
    
    if (length < 0 || length > 4) {
        ESP_LOGW(TAG, "Invalid PIN length: %d", length);
        return;
    }
    
    char pin_display[17] = "PIN: ";
    
    // Add asterisks for each digit entered
    for (int i = 0; i < length; i++) {
        pin_display[5 + i] = '*';
    }
    pin_display[5 + length] = '\0';
    
    // Write to row 1 only (keep status on row 0)
    lcd_display_write(pin_display, 1);
    ESP_LOGI(TAG, "PIN entry: %d digits", length);
}

void lcd_display_clear_pin_entry(void)
{
    // Cancel any active message timer when clearing PIN entry
    if (message_timer != NULL) {
        esp_timer_handle_t timer_to_delete = message_timer;
        message_timer = NULL;  // Clear handle first to prevent callback from accessing
        message_active = false;
        
        // Stop and delete timer (ignore errors if already fired)
        esp_timer_stop(timer_to_delete);
        esp_timer_delete(timer_to_delete);
    }
    
    lcd_display_write("Ready", 1);
    ESP_LOGI(TAG, "PIN entry cleared");
}

void lcd_display_show_checking(void)
{
    // Cancel any active message timer when starting PIN check
    if (message_timer != NULL) {
        esp_timer_handle_t timer_to_delete = message_timer;
        message_timer = NULL;  // Clear handle first to prevent callback from accessing
        message_active = false;
        
        // Stop and delete timer (ignore errors if already fired)
        esp_timer_stop(timer_to_delete);
        esp_timer_delete(timer_to_delete);
    }
    
    lcd_display_write("Checking...", 1);
    ESP_LOGI(TAG, "Showing checking message");
}

// Timer callback to restore full state display after message
static void message_timeout_callback(void *arg)
{
    ESP_LOGI(TAG, "Message timeout - restoring state: %d", restore_state);
    message_active = false;
    // Restore the full state display (both rows and backlight color)
    lcd_display_show_state(restore_state);
}

void lcd_display_show_message(const char *message, uint32_t duration_ms, safe_state_t state)
{
    // Store state to restore after timeout
    restore_state = state;
    
    // Mark message as active to prevent PIN entry updates
    message_active = true;
    
    // Display the message on row 1
    lcd_display_write(message, 1);
    ESP_LOGI(TAG, "Showing message: %s (timeout: %lu ms)", message, duration_ms);
    
    // Stop existing timer if running
    if (message_timer != NULL) {
        esp_timer_handle_t timer_to_delete = message_timer;
        message_timer = NULL;  // Clear handle first to prevent callback from accessing
        
        // Stop and delete timer (ignore errors if already fired)
        esp_timer_stop(timer_to_delete);
        esp_timer_delete(timer_to_delete);
    }
    
    // Create timer for message timeout
    const esp_timer_create_args_t timer_args = {
        .callback = &message_timeout_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lcd_msg_timer"
    };
    
    esp_err_t err = esp_timer_create(&timer_args, &message_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create message timer: %s", esp_err_to_name(err));
        return;
    }
    
    // Start one-shot timer
    err = esp_timer_start_once(message_timer, duration_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start message timer: %s", esp_err_to_name(err));
        esp_timer_delete(message_timer);
        message_timer = NULL;
    }
}

void lcd_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "LCD task started (Priority 2)");

    // Initialize LCD
    if (!lcd_display_init()) {
        ESP_LOGE(TAG, "Failed to initialize LCD, task exiting");
        vTaskDelete(NULL);
        return;
    }

    // Show initial locked state
    lcd_display_show_state(STATE_LOCKED);

    while (1) {
        lcd_cmd_t cmd;
        if (receive_lcd_cmd(&cmd, 100)) {
            switch (cmd.type) {
                case LCD_CMD_SHOW_STATE:
                    lcd_display_show_state(cmd.state);
                    break;
                case LCD_CMD_SHOW_PIN_ENTRY:
                    lcd_display_show_pin_entry(cmd.pin_length);
                    break;
                case LCD_CMD_CLEAR_PIN_ENTRY:
                    lcd_display_clear_pin_entry();
                    break;
                case LCD_CMD_SHOW_MESSAGE:
                    lcd_display_show_message(cmd.message, cmd.duration_ms, cmd.state);
                    break;
                case LCD_CMD_SHOW_CHECKING:
                    lcd_display_show_checking();
                    break;
            }
        }
    }
}
