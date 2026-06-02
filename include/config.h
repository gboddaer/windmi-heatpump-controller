/**
 * @file config.h
 * @brief Configuration constants for Windmi Controller
 *
 * Verified against master branch (src/config.h).
 */

#ifndef WINDMI_CONFIG_H
#define WINDMI_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

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

// Serial Port Configuration
#define SERIAL_DEFAULT_BAUD      9600
#define SERIAL_DEFAULT_STOP_BITS 1

// Running Mode Values for SETTING (register 0x002C - writable)
// Only modes 0, 1, 2 should be SET on this register
#define MODE_SET_OFF              0      // Off
#define MODE_SET_COOL_DHW         1      // Cool + DHW
#define MODE_SET_HEAT_DHW         2      // Heat + DHW

// Running Status Values for READING (register 0x002D - read-only)
// These values differ from the writable mode values!
#define MODE_STATUS_OFF           0      // Off
#define MODE_STATUS_COOL          1      // Cooling
#define MODE_STATUS_HEAT          2      // Heating
#define MODE_STATUS_DHW           4      // DHW only
#define MODE_STATUS_DEFROST       7      // Defrost cycle
#define MODE_STATUS_ANTIFREEZE   20      // Home anti-freeze

// Modbus Register Addresses
// Format: ADDRESS  // Access: R/W/RW - Description
// R = Read-only, W = Write-only, RW = Read/Write
//
// Temperature values are in 0.1°C units (e.g., 450 = 45.0°C)
//
// === READ-ONLY REGISTERS ===
#define REG_DEVICE_TYPE           0x1006  // R  - Device type identifier
#define REG_OUTDOOR_TEMP          0x0001  // R  - Outdoor temperature (0.1°C)
#define REG_INDOOR_TEMP           0x0002  // R  - Indoor temperature (0.1°C)
#define REG_ENTERING_WATER_TEMP   0x0003  // R  - Entering water temperature (0.1°C)
#define REG_LEAVING_WATER_TEMP    0x0004  // R  - Leaving water temperature (0.1°C)
#define REG_WATER_CONTROL_POINT   0x0033  // R  - Water control point status
#define REG_RUNNING_STATUS        0x002D  // R  - Current running status (0-5 normal, 7=defrost, 20=antifreeze)
#define REG_DHW_MODE_STATUS       0x00C9  // R  - DHW mode: 0=Eco, 1=Anti Legionella, 2=Regular
#define REG_DHW_PRIORITY          0x02BF  // RW - DHW priority flag (0=Automatic, 1=DHW priority)
//
// === READ/WRITE REGISTERS ===
#define REG_RUNNING_MODE          0x002C  // RW - Set mode: 0=Off, 1=Cool+DHW, 2=Heat+DHW (ONLY 0,1,2 allowed!)
#define REG_HEATING_TARGET        0x0191  // RW - Heating target temperature (0.1°C)
#define REG_DHW_TARGET            0x0194  // RW - DHW target temperature (0.1°C)
#define REG_OCCUPANCY_MODE        0x0029  // RW - Occupancy mode: 0=Away, 1=Sleep, 2=Home
//
// === POWER MONITORING REGISTERS (READ-ONLY) ===
#define REG_AC_CURRENT            0x1014  // R  - AC current (Display * 2 = Actual Amps)
#define REG_DC_CURRENT            0x1015  // R  - DC current (Display * 4 = Actual Amps)
#define REG_AC_VOLTAGE            0x1016  // R  - AC voltage (Display = Actual Volts)
#define REG_DC_VOLTAGE            0x1017  // R  - DC voltage (Display / 2 = Actual Volts)
//
// === DHW TANK TEMPERATURE ===
#define REG_DHW_TANK_TEMP         0x00CE  // R  - DHW tank temperature (0.1°C)

// Self-test configuration
#define SELFTEST_DHW_TARGET_TEMP  45.0f

// Temperature minimums for single-purpose modes (used in control logic)
#define DHW_TARGET_MIN              40.0f
#define HEATING_TARGET_MIN          25.0f

// Queue sizes
#define STATUS_QUEUE_SIZE           32
#define CMD_QUEUE_SIZE              16

#ifdef __cplusplus
}
#endif

#endif /* WINDMI_CONFIG_H */