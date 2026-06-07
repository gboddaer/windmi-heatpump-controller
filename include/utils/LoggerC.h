/**
 * @file utils/LoggerC.h
 * @brief C API for structured logging
 *
 * Callable from .c files. Use the WINDMI_C_LOG() macro.
 */

#ifndef WINDMI_UTILS_LOGGER_C_H_
#define WINDMI_UTILS_LOGGER_C_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  WINDMI_LOG_TRACE = 0,
  WINDMI_LOG_DEBUG = 1,
  WINDMI_LOG_INFO = 2,
  WINDMI_LOG_WARN = 3,
  WINDMI_LOG_ERROR = 4,
  WINDMI_LOG_FATAL = 5
} windmi_log_level_t;

/**
 * @brief Check if a given level would pass the filter
 *
 * Thread-safe. Call before formatting to avoid work.
 */
int windmi_should_log(int level);

/**
 * @brief Emit a formatted log message
 *
 * Called by WINDMI_C_LOG() after should_log() check.
 * The message is formatted inline via vsnprintf.
 */
void windmi_log(int level, const char* tag, const char* file, int line, const char* func,
                const char* fmt, ...);

/**
 * @brief C logging macro
 *
 * Usage: WINDMI_C_LOG(WINDMI_LOG_INFO, LOG_TAG_MODBUS, "Got %d bytes", n);
 *
 * The should_log() check gates the entire block.
 * ##__VA_ARGS__ is a GNU extension (acceptable for GCC/Clang).
 */
#define WINDMI_C_LOG(level, tag, fmt, ...)                                                         \
  do                                                                                               \
  {                                                                                                \
    if (windmi_should_log(level))                                                                  \
    {                                                                                              \
      windmi_log(level, tag, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);                    \
    }                                                                                              \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif // WINDMI_UTILS_LOGGER_C_H
