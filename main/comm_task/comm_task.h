#ifndef COMM_TASK_H
#define COMM_TASK_H

// Main comm task function
void comm_task(void *pvParameters);

// Handle incoming MQTT command (called by MQTT event handler)
void handle_mqtt_command(const char *data, int len);

#endif
