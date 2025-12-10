#include "keypad.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "KEYPAD";

// GPIO pin definitions for 4x4 keypad
// Rows: Outputs (driven low one at a time)
#define ROW1_PIN GPIO_NUM_13
#define ROW2_PIN GPIO_NUM_12
#define ROW3_PIN GPIO_NUM_14
#define ROW4_PIN GPIO_NUM_27

// Columns: Inputs (with pull-ups)
#define COL1_PIN GPIO_NUM_26
#define COL2_PIN GPIO_NUM_25
#define COL3_PIN GPIO_NUM_33
#define COL4_PIN GPIO_NUM_32

// Array of row and column pins for easy iteration
static const gpio_num_t row_pins[4] = {ROW1_PIN, ROW2_PIN, ROW3_PIN, ROW4_PIN};
static const gpio_num_t col_pins[4] = {COL1_PIN, COL2_PIN, COL3_PIN, COL4_PIN};

// Keypad layout mapping (standard 4x4 layout)
static const char key_map[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

// Debouncing state
static char last_key = '\0';
static uint8_t debounce_count = 0;
#define DEBOUNCE_THRESHOLD 2  // Key must be stable for 2 consecutive reads

void keypad_init(void)
{
    ESP_LOGI(TAG, "Initializing 4x4 keypad matrix");
    
    // Configure row pins as outputs (initially high)
    gpio_config_t row_conf = {
        .pin_bit_mask = ((1ULL << ROW1_PIN) | (1ULL << ROW2_PIN) | 
                         (1ULL << ROW3_PIN) | (1ULL << ROW4_PIN)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&row_conf);
    
    // Set all rows high initially
    for (int i = 0; i < 4; i++) {
        gpio_set_level(row_pins[i], 1);
    }
    
    // Configure column pins as inputs with pull-ups
    gpio_config_t col_conf = {
        .pin_bit_mask = ((1ULL << COL1_PIN) | (1ULL << COL2_PIN) | 
                         (1ULL << COL3_PIN) | (1ULL << COL4_PIN)),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&col_conf);
    
    ESP_LOGI(TAG, "Keypad initialized successfully");
    ESP_LOGI(TAG, "Row pins: %d, %d, %d, %d", ROW1_PIN, ROW2_PIN, ROW3_PIN, ROW4_PIN);
    ESP_LOGI(TAG, "Col pins: %d, %d, %d, %d", COL1_PIN, COL2_PIN, COL3_PIN, COL4_PIN);
}

char keypad_get_key(void)
{
    char detected_key = '\0';
    
    // Scan each row
    for (int row = 0; row < 4; row++) {
        // Set all rows high first
        for (int i = 0; i < 4; i++) {
            gpio_set_level(row_pins[i], 1);
        }
        
        // Drive current row low
        gpio_set_level(row_pins[row], 0);
        
        // Delay to let the signal stabilize
        vTaskDelay(1 / portTICK_PERIOD_MS);
        
        for (int col = 0; col < 4; col++) {
            int level = gpio_get_level(col_pins[col]);
            
            // If col is low, key at this row/col is pressed
            if (level == 0) {
                detected_key = key_map[row][col];
                break;
            }
        }
        
        if (detected_key != '\0') {
            break;
        }
    }
    
    // Set all rows high after scanning
    for (int i = 0; i < 4; i++) {
        gpio_set_level(row_pins[i], 1);
    }
    
    // Debouncing logic
    if (detected_key == last_key) {
        debounce_count++;
        if (debounce_count >= DEBOUNCE_THRESHOLD) {
            debounce_count = DEBOUNCE_THRESHOLD; 
            return detected_key;
        }
    } else {
        // Key changed, reset debounce counter
        last_key = detected_key;
        debounce_count = 0;
    }
    
    // Key not stable yet
    return '\0';
}

void keypad_demo(void)
{
    ESP_LOGI(TAG, "Starting keypad demo...");
    ESP_LOGI(TAG, "Press keys on the keypad. Press and hold to test debouncing.");
    
    char last_printed_key = '\0';
    
    while (1) {
        char key = keypad_get_key();
        
        if (key != '\0' && key != last_printed_key) {
            ESP_LOGI(TAG, "Key pressed: '%c'", key);
            last_printed_key = key;
        } else if (key == '\0' && last_printed_key != '\0') {
            ESP_LOGI(TAG, "Key released");
            last_printed_key = '\0';
        }
        
        // Poll every 50ms (adjust as needed)
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}