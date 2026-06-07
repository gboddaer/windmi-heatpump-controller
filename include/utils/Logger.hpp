/**
 * @file utils/Logger.hpp
 * @brief C++ structured logging with level filtering and multiple outputs
 *
 * Features:
 *  - Level-based filtering: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
 *  - Multiple outputs (console + optional file)
 *  - Thread-safe via mutex
 *  - Zero overhead when level filtered (check before formatting)
 *  - Structured output: [timestamp] [LEVEL] [TAG] message (file:line)
 *  - C++ macros: WINDMI_LOG_INFO, WINDMI_LOG_DEBUG, etc.
 */

#ifndef WINDMI_UTILS_LOGGER_HPP_
#define WINDMI_UTILS_LOGGER_HPP_

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Platform-specific includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Platform abstraction for mutex (must be before Logger class)
#include "utils/Platform.hpp"

namespace windmi {

/**
 * @brief Log severity levels
 */
enum class LogLevel {
  Trace = 0, // Very verbose, debug-only
  Debug = 1, // Detailed flow
  Info = 2,  // General info (default in production)
  Warn = 3,  // Warning conditions
  Error = 4, // Errors (goes to stderr)
  Fatal = 5, // Critical failures (exit)
};

/**
 * @brief A single log entry
 */
struct LogEntry
{
  LogLevel level;
  const char* tag;
  const char* message;
  const char* file;
  int line;
  const char* function;
  std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Log output interface
 *
 * Implement this interface to create custom log outputs.
 */
class ILogOutput
{
public:
  virtual ~ILogOutput() = default;
  virtual void write(const LogEntry& entry) = 0;
};

/**
 * @brief Console output: INFO+ to stdout, WARN+ to stderr
 *
 * When attached to a terminal, optional ANSI color codes may be applied.
 */
class ConsoleLogOutput : public ILogOutput
{
public:
  void write(const LogEntry& entry) override;
};

/**
 * @brief File output: all levels written to a single file
 *
 * Simple append mode; no rotation. Suitable for embedded systems that
 * restart on deploy.
 */
class FileLogOutput : public ILogOutput
{
public:
  explicit FileLogOutput(const std::string& path);
  ~FileLogOutput() override;

  void write(const LogEntry& entry) override;

private:
  std::FILE* mFile;
};

/*
 * Mutex and LockGuard are now in Platform.hpp to be platform-agnostic
 * and work with both MinGW (without std::mutex) and full C++17 implementations
 */

/**
 * @brief Singleton logger
 *
 * Default construction installs a ConsoleLogOutput at INFO level.
 * Main() calls setOutput()/addOutput() to configure at startup.
 */
class Logger
{
public:
  /**
   * @brief Get the singleton instance
   */
  static Logger& instance();

  /**
   * @brief Set the minimum level (log calls below this are ignored)
   */
  void setLevel(LogLevel level);

  /**
   * @brief Check if a given level would pass the filter
   *
   * Thread-safe via atomic. Call before formatting.
   */
  bool shouldLog(LogLevel level) const;

  /**
   * @brief Add an output destination
   *
   * Thread-safe: adds under lock.
   */
  void addOutput(std::unique_ptr<ILogOutput> output);

  /**
   * @brief Clear all outputs and add a single output
   *
   * Use this to replace the default ConsoleLogOutput.
   */
  void setOutput(std::unique_ptr<ILogOutput> output);

  /**
   * @brief Emit a log entry (called by macros after formatting)
   *
   * The message is already formatted by the caller (macro).
   */
  void log(LogLevel level, const char* tag, const char* file, int line, const char* function,
           const char* message) const;

  /**
   * @brief Format a log level as a fixed-width string
   */
  std::string formatLevel(LogLevel level) const;

  /**
   * @brief Format a timestamp as "YYYY-MM-DD HH:MM:SS.mmm"
   */
  static std::string formatTimestamp(const std::chrono::system_clock::time_point& tp);

private:
  Logger();
  ~Logger();
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  mutable Mutex mMutex;
  std::atomic<int> mLevel;
  std::vector<std::unique_ptr<ILogOutput>> mOutputs;
};

// Alias for compatibility
using StdoutLogOutput = ConsoleLogOutput;

} // namespace windmi

// ─────────────────────────────────────────────────────────────────────
// C++ Macros
//
// Usage: WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set mode to %d", mode);
//
// The shouldLog() check gates the entire block so that when a level is
// filtered out, zero string formatting work is done.
//
// GCC/Clang extension: ##__VA_ARGS__ suppresses the comma when
// __VA_ARGS__ is empty. This is accepted per the project plan.
// -Wno-variadic-macros is set in CMakeLists.txt to suppress the pedantic warning.

#define WINDMI_LOG(level, tag, fmt, ...)                                                           \
  do                                                                                               \
  {                                                                                                \
    if (windmi::Logger::instance().shouldLog(level))                                               \
    {                                                                                              \
      char _logbuf[512];                                                                           \
      snprintf(_logbuf, sizeof(_logbuf), fmt, ##__VA_ARGS__);                                      \
      windmi::Logger::instance().log(level, tag, __FILE__, __LINE__, __func__, _logbuf);           \
    }                                                                                              \
  } while (0)

#define WINDMI_LOG_TRACE(tag, fmt, ...) WINDMI_LOG(windmi::LogLevel::Trace, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_DEBUG(tag, fmt, ...) WINDMI_LOG(windmi::LogLevel::Debug, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_INFO(tag, fmt, ...) WINDMI_LOG(windmi::LogLevel::Info, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_WARN(tag, fmt, ...) WINDMI_LOG(windmi::LogLevel::Warn, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_ERROR(tag, fmt, ...) WINDMI_LOG(windmi::LogLevel::Error, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_FATAL(tag, fmt, ...) WINDMI_LOG(windmi::LogLevel::Fatal, tag, fmt, ##__VA_ARGS__)

#endif // WINDMI_UTILS_LOGGER_HPP
