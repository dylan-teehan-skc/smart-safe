# Smart Safe

An ESP32-based smart safe system with PIN authentication, tamper detection, and remote monitoring via MQTT.

## Features

- **4-Digit PIN Authentication** - Keypad input with LCD feedback
- **Tamper Detection** - MPU6050 accelerometer triggers alarm on movement
- **RGB LCD Display** - Real-time status display with color-coded backlight
- **Remote Monitoring** - Node-RED dashboard via MQTT
- **Remote Control** - Lock/unlock and reset alarm from dashboard
- **Discord Alerts** - Webhook notifications on alarm trigger

## Hardware

| Component | GPIO Pins | Description |
|-----------|-----------|-------------|
| 4x4 Matrix Keypad | Rows: 13,12,14,27 / Cols: 26,25,33,32 | PIN entry |
| MPU6050 Accelerometer | SDA: 21, SCL: 22 | Tamper detection |
| RGB LCD (DFRobot DFR0464) | SDA: 21, SCL: 22 (shared I2C) | Status display |
| Red LED | GPIO 4 | Locked/Alarm indicator |
| Green LED | GPIO 18 | Unlocked indicator |

## Software Requirements

- ESP-IDF v5.5.1+
- Node-RED with `node-red-dashboard`
- Mosquitto MQTT broker

## Configuration

1. Copy `main/config.example.h` to `main/config.h`
2. Edit with your credentials:

```c
#define WIFI_SSID          "YourWiFiSSID"
#define WIFI_PASSWORD      "YourWiFiPassword"
#define CORRECT_PIN        "1234"
#define MAX_WRONG_ATTEMPTS 3
#define MQTT_BROKER_URI    "mqtt://your-broker:1883"
#define MQTT_DEVICE_ID     "smartsafe01"
```

## Usage

### PIN Entry
- Enter 4-digit PIN on keypad
- Press `#` to submit
- Press `*` to clear entry

### States
| State | LED | LCD Backlight | Description |
|-------|-----|---------------|-------------|
| LOCKED | Red ON | Red | Safe is locked |
| UNLOCKED | Green ON | Green | Safe is unlocked |
| ALARM | Red FLASHING | Red | Tamper detected or 3+ wrong PINs |

### Resetting Alarm
- Enter correct PIN on keypad, OR
- Click "Reset Alarm" on Node-RED dashboard

## MQTT Protocol

### Topics
- **Telemetry**: `smartsafe/<device_id>/telemetry` (ESP32 -> Broker)
- **Commands**: `smartsafe/<device_id>/command` (Broker -> ESP32)

### Telemetry Messages
```json
{"ts":1234567890,"state":"locked","event":"state_change"}
{"ts":1234567890,"state":"alarm","event":"movement","movement_amount":1.5}
{"ts":1234567890,"state":"locked","event":"code_entry","code_ok":false}
```

### Command Messages
```json
{"command":"lock"}
{"command":"unlock"}
{"command":"set_code","code":"1234"}
{"command":"reset_alarm"}
```

## Architecture

```
+-------------------------------------------------------------+
|                         ESP32                                |
|                                                              |
|  +----------+  +----------+  +----------+  +----------+     |
|  | Keypad   |  | Sensor   |  | Control  |  |  Comm    |     |
|  |  Task    |->|  Task    |->|  Task    |<-|  Task    |<--MQTT
|  +----------+  +----------+  +----------+  +----------+     |
|                                   |                          |
|                    +----------+  +----------+               |
|                    |   LED    |  |   LCD    |               |
|                    |   Task   |  |   Task   |               |
|                    +----------+  +----------+               |
+-------------------------------------------------------------+
```

### FreeRTOS Tasks
| Task | Priority | Stack | Description |
|------|----------|-------|-------------|
| control_task | 4 | 4096 | State machine, PIN verification |
| comm_task | 3 | 8192 | WiFi, MQTT, telemetry |
| lcd_task | 2 | 4096 | LCD display updates |
| led_task | 2 | 2048 | LED control |
| keypad_task | 3 | 2048 | Keypad scanning |
| sensor_task | 3 | 4096 | MPU6050 polling |

### Dashboard Features
- Lock/Unlock buttons
- Reset Alarm button
- Current state display
- Event history
- Movement graph
- Discord webhook alerts

## Project Structure

```
smart-safe/
├── main/
│   ├── main.c                 # Entry point
│   ├── config.h               # Configuration (gitignored)
│   ├── config.example.h       # Config template
│   ├── control_task/          # State machine, command handling
│   ├── comm_task/             # WiFi, MQTT
│   ├── queue_manager/         # FreeRTOS queues
│   ├── json_protocol/         # JSON serialization
│   ├── state_machine/         # State transitions
│   ├── pin_manager/           # PIN verification
│   ├── keypad/                # 4x4 keypad driver
│   ├── lcd_display/           # LCD controller
│   ├── led/                   # LED control
│   └── mpu6050/               # Accelerometer driver
├── docs/
│   ├── system-diagram.md      # Architecture diagrams
│   └── plan.md                # Project plan
└── README.md
```

