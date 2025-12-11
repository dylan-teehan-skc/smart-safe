#ifndef CONFIG_H
#define CONFIG_H

// WiFi credentials
#define WIFI_SSID      "your_wifi_ssid"
#define WIFI_PASSWORD  "your_wifi_password"

// MQTT broker settings
#define MQTT_BROKER_URI  "mqtt://alderaan.software-engineering.ie:1883"
#define MQTT_DEVICE_ID   "smartsafe01"

// MQTT topics (built from device ID)
#define MQTT_TOPIC_TELEMETRY "smartsafe/" MQTT_DEVICE_ID "/telemetry"
#define MQTT_TOPIC_COMMAND   "smartsafe/" MQTT_DEVICE_ID "/command"

#endif
