#ifndef CONFIG_H
#define CONFIG_H

// Load configuration from .env file
void config_init(void);

// Get the correct PIN for the safe
const char* config_get_correct_pin(void);

#endif // CONFIG_H

