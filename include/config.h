/**
 * @file config.h
 * @brief Configuration constants
 */

#ifndef WINDMI_CONFIG_H
#define WINDMI_CONFIG_H

/* Modbus register addresses */
#define REG_DHW_TEMP                0x0012
#define REG_HEATING_TEMP            0x0014
#define REG_DHW_PRIORITY            0x02BF
#define REG_RUNNING_MODE            0x002C
#define REG_RUNNING_STATUS          0x002D
#define REG_AC_CURRENT              0x1014
#define REG_DC_CURRENT              0x1015
#define REG_AC_VOLTAGE              0x1016
#define REG_DC_VOLTAGE              0x1017
#define REG_AC_POWER                0x1018

/* Working modes */
#define MODE_OFF                    0
#define MODE_DHW_ONLY               1
#define MODE_HEATING_ONLY           2
#define MODE_DHW_HEATING            3

/* Priority modes */
#define PRIORITY_AUTO               0
#define PRIORITY_DHW                1

/* Temperature ranges */
#define DHW_TEMP_MIN                40.0f
#define DHW_TEMP_MAX                63.0f
#define HEATING_TEMP_MIN            25.0f
#define HEATING_TEMP_MAX            63.0f

/* Temperature minimums for single-purpose modes */
#define DHW_TARGET_MIN              40.0f
#define HEATING_TARGET_MIN          25.0f

/* Default values */
#define DEFAULT_DHW_TARGET          45.0f
#define DEFAULT_HEATING_TARGET      40.0f
#define DEFAULT_PRIORITY            1  /* DHW priority */

/* Web server */
#define WEB_SERVER_IP               "0.0.0.0"

/* Queue sizes */
#define STATUS_QUEUE_SIZE           32
#define CMD_QUEUE_SIZE              16

#endif /* WINDMI_CONFIG_H */
