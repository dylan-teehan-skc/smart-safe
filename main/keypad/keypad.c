#include "keypad.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static const char *TAG = "KEYPAD";

// GPIO pin definitions for 4x4 keypad
// Rows: Outputs (driven low one at a time)
#define ROW1_PIN GPIO_NUM_2
#define ROW2_PIN GPIO_NUM_5
#define ROW3_PIN GPIO_NUM_13
#define ROW4_PIN GPIO_NUM_10

// Columns: Inputs (with pull-ups)
#define COL1_PIN GPIO_NUM_9
#define COL2_PIN GPIO_NUM_27
#define COL3_PIN GPIO_NUM_26
#define COL4_PIN GPIO_NUM_25

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

// Queue to send key events from ISR to task context
static QueueHandle_t keypad_queue = NULL;
#define KEYPAD_QUEUE_SIZE 10

// Debounce timer for ISR
static volatile TickType_t last_interrupt_time = 0;

#define DEBOUNCE_DELAY_MS 50

// ISR handler - called when any column pin goes LOW
// IRAM_ATTR places in internal RAM -> faster than flash memory
static void IRAM_ATTR keypad_isr_handler(void *arg)
{
    TickType_t current_time = xTaskGetTickCountFromISR();
    
    // Debounce check
    if ((current_time - last_interrupt_time) < (DEBOUNCE_DELAY_MS / portTICK_PERIOD_MS)) {
        return;
    }
    last_interrupt_time = current_time;
    
    // Set flag to trigger scan in task context (we are in ISR here)
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t dummy = 1;
    xQueueSendFromISR(keypad_queue, &dummy, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Scan the keypad matrix to detect which key is pressed
static char keypad_scan(void)
{
    char detected_key = '\0';
    
    // Scan each row
    for (int row = 0; row < 4; row++) {
        // Set all rows HIGH
        for (int i = 0; i < 4; i++) {
            gpio_set_level(row_pins[i], 1);
        }
        
        // Drive current row LOW
        gpio_set_level(row_pins[row], 0);
        
        // Short delay for stable signal (100 us)
        ets_delay_us(100);
        
        // Read all columns (if LOW then key pressed)
        for (int col = 0; col < 4; col++) {
            if (gpio_get_level(col_pins[col]) == 0) {
                detected_key = key_map[row][col];
                break;
            }
        }
        
        if (detected_key != '\0') {
            break;
        }
    }
    
    // Return all rows to LOW for next interrupt detection
    for (int i = 0; i < 4; i++) {
        gpio_set_level(row_pins[i], 0);
    }
    
    return detected_key;
}

void keypad_init(void)
{
    ESP_LOGI(TAG, "Initializing 4x4 keypad with interrupts");
    
    // Create queue for key events
    keypad_queue = xQueueCreate(KEYPAD_QUEUE_SIZE, sizeof(uint8_t));
    if (keypad_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create keypad queue");
        return;
    }
    
    // Row pins configed as outputs (low by default)
    gpio_config_t row_conf = {
        .pin_bit_mask = ((1ULL << ROW1_PIN) | (1ULL << ROW2_PIN) | 
                         (1ULL << ROW3_PIN) | (1ULL << ROW4_PIN)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&row_conf);
    
    // Set all rows LOW initially (key press will pull column LOW)
    for (int i = 0; i < 4; i++) {
        gpio_set_level(row_pins[i], 0);
    }
    
    // Configure column pins as inputs with pull-ups and FALLING edge interrupts
    gpio_config_t col_conf = {
        .pin_bit_mask = ((1ULL << COL1_PIN) | (1ULL << COL2_PIN) | 
                         (1ULL << COL3_PIN) | (1ULL << COL4_PIN)),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // Interrupt on falling edge
    };
    gpio_config(&col_conf);
    
    // Install GPIO ISR service
    esp_err_t isr_ret = gpio_install_isr_service(0);  
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {  
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_ret));  
        return;  
    }  
    
    // Add ISR handlers for each column pin
    for (int i = 0; i < 4; i++) {
        gpio_isr_handler_add(col_pins[i], keypad_isr_handler, (void*)(col_pins[i]));
    }
    
    ESP_LOGI(TAG, "Keypad initialized with interrupts");
    ESP_LOGI(TAG, "Row pins: %d, %d, %d, %d", ROW1_PIN, ROW2_PIN, ROW3_PIN, ROW4_PIN);
    ESP_LOGI(TAG, "Col pins: %d, %d, %d, %d", COL1_PIN, COL2_PIN, COL3_PIN, COL4_PIN);
}

char keypad_get_key(void)
{
    uint8_t dummy;
    
    // Check for interrupt signal (non-blocking, 0 timeout)
    if (xQueueReceive(keypad_queue, &dummy, 0) == pdTRUE) {
        // Interrupt occurred, scan matrix
        char key = keypad_scan();
        
        // Additional debounce: wait a bit and scan again
        vTaskDelay(20 / portTICK_PERIOD_MS);
        char key2 = keypad_scan();
        
        if (key == key2 && key != '\0') {
            return key;
        }
    }
    
    return '\0';
}

char keypad_wait_for_key(uint32_t timeout_ms)
{
    uint8_t dummy;
    TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : (timeout_ms / portTICK_PERIOD_MS);
    
    // Wait for interrupt signal (blocking)
    if (xQueueReceive(keypad_queue, &dummy, timeout_ticks) == pdTRUE) {
        char key = keypad_scan();
        
        // Additional debounce
        vTaskDelay(20 / portTICK_PERIOD_MS);
        char key2 = keypad_scan();
        
        if (key == key2 && key != '\0') {
            return key;
        }
    }
    
    return '\0';  // Timeout or no valid key
}

// void keypad_demo(void)
// {
//     ESP_LOGI(TAG, "Starting interrupt-based keypad demo...");
//     ESP_LOGI(TAG, "Press keys on the keypad. CPU sleeps between presses.");
    
//     while (1) {
//         // Blocking wait - CPU can sleep until key press
//         char key = keypad_wait_for_key(0);  // 0 = wait forever
        
//         if (key != '\0') {
//             ESP_LOGI(TAG, "Key pressed: '%c'", key);
            
//             // Wait for key release (all columns HIGH)
//             while (1) {
//                 bool all_released = true;
//                 for (int i = 0; i < 4; i++) {
//                     if (gpio_get_level(col_pins[i]) == 0) {
//                         all_released = false;
//                         break;
//                     }
//                 }
//                 if (all_released) {
//                     ESP_LOGI(TAG, "Key released");
//                     break;
//                 }
//                 vTaskDelay(10 / portTICK_PERIOD_MS);
//             }
//         }
//     }
// }