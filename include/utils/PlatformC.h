/**
 * @file utils/PlatformC.h
 * @brief C API for platform abstraction functions
 *
 * Callable from .c files. Provides C-linkage wrappers
 * for windmi::platform functions.
 */

#ifndef WINDMI_UTILS_PLATFORM_C_H
#define WINDMI_UTILS_PLATFORM_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cross-platform millisecond sleep. */
void windmi_sleep_ms(unsigned int ms);

#ifdef __cplusplus
}
#endif

#endif // WINDMI_UTILS_PLATFORM_C_H