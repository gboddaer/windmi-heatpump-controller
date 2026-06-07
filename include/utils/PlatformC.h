/**
 * @file utils/PlatformC.h
 * @brief C API for platform abstraction functions
 */

#ifndef WINDMI_UTILS_PLATFORM_C_H_
#define WINDMI_UTILS_PLATFORM_C_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cross-platform millisecond sleep. */
void windmi_sleep_ms(unsigned int ms);

/** Opaque cross-platform serial port handle. */
typedef struct WindmiSerialPort WindmiSerialPort;

WindmiSerialPort* windmi_serial_open(const char* device, int baud, char parity, int stop_bits,
                                     bool rs485_enabled);
void windmi_serial_close(WindmiSerialPort* port);
int windmi_serial_read(WindmiSerialPort* port, uint8_t* buffer, size_t len,
                       unsigned int timeout_ms);
int windmi_serial_write(WindmiSerialPort* port, const uint8_t* buffer, size_t len);
void windmi_serial_flush(WindmiSerialPort* port);

#ifdef __cplusplus
}
#endif

#endif // WINDMI_UTILS_PLATFORM_C_H
