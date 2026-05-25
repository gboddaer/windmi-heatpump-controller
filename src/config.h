#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

// Network Configuration
#define WEB_SERVER_PORT        8080
#define WEB_SERVER_IP          "0.0.0.0"

// Modbus Configuration
// Waveshare gateway in transparent mode: raw Modbus RTU frames over TCP
// No MBAP header. Port 8899 (not 502).
// MODBUS_GATEWAY_IP can be overridden via --ip command-line argument
#define MODBUS_GATEWAY_IP      "192.168.123.10"
#define MODBUS_GATEWAY_PORT    8899
#define MODBUS_SLAVE_ID        11
#define MODBUS_POLL_INTERVAL_S 30

// Only function code 0x03 (read holding registers) and 0x06 (write single
// register) are supported. Function code 0x04 returns exception 04 on this
// firmware.

// Temperature Ranges (from Rotenso register specification)
#define DHW_TEMP_MIN           40.0f      // raw 400 = 40.0 degC
#define DHW_TEMP_MAX           63.0f      // raw 630 = 63.0 degC
#define HEATING_TEMP_MIN       25.0f      // raw 250 = 25.0 degC
#define HEATING_TEMP_MAX       63.0f      // raw 630 = 63.0 degC

// Control Loop
#define CONTROL_LOOP_INTERVAL_S 30
#define DHW_HYSTERESIS_C       3.0f
#define HEATING_HYSTERESIS_C   1.0f

// Timeouts and Retries
#define MODBUS_TIMEOUT_MS      1000
#define MODBUS_MAX_RETRIES     3
#define MODBUS_RECONNECT_INTERVAL_S 10

// SPSC Queue sizes
#define CMD_QUEUE_SIZE         16
#define STATUS_QUEUE_SIZE      4

// Running Mode Values (verified from hardware)
// NOTE: 1=Cool, 2=Heat — this differs from typical Modbus conventions
#define MODE_OFF               0
#define MODE_COOL              1
#define MODE_HEAT              2
#define MODE_DHW               4
#define MODE_DEFROST           7
#define MODE_HOME_ANTIFREEZE   20

// Modbus Register Addresses
#define REG_OUTDOOR_TEMP          0x0001
#define REG_INDOOR_TEMP           0x0002
#define REG_ENTERING_WATER_TEMP   0x0003
#define REG_LEAVING_WATER_TEMP    0x0004
#define REG_RUNNING_MODE          0x002D
#define REG_DHW_TARGET            0x0194
#define REG_HEATING_TARGET        0x0191
#define REG_DHW_PRIORITY          0x028F
#define REG_DHW_TANK_TEMP         0x1C5B

#endif // CONFIG_H
