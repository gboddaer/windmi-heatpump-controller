/**
 * @file utils/LoggerC.cpp
 * @brief C bridge for structured logging
 *
 * Implements the extern "C" functions declared in LoggerC.h,
 * delegating to the C++ Logger singleton.
 */

#include "utils/Logger.hpp"
#include "utils/LoggerC.h"

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>

extern "C" {

int windmi_should_log(int level)
{
  return windmi::Logger::instance().shouldLog(static_cast<windmi::LogLevel>(level));
}

void windmi_log(int level, const char* tag, const char* file, int line, const char* func,
                const char* fmt, ...)
{
  char buf[512];

  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  windmi::Logger::instance().log(static_cast<windmi::LogLevel>(level), tag, file, line, func, buf);
}

} // extern "C"
