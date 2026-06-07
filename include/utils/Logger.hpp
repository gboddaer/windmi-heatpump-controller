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

#ifndef WINDMI_UTILS_LOGGER_HPP
#define WINDMI_UTILS_LOGGER_HPP

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace windmi {

/**
 * @brief Log severity levels
 */
enum class LogLevel {
    TRACE = 0,  // Very verbose, debug-only
    DEBUG = 1,  // Detailed flow
    INFO  = 2,  // General info (default in production)
    WARN  = 3,  // Warning conditions
    ERROR = 4,  // Errors (goes to stderr)
    FATAL = 5,  // Critical failures (exit)
};

/**
 * @brief A single log entry
 */
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel    level;
    const char* tag;       // Component tag (static string, not owned)
    std::string message;
    const char* file;      // Static string from __FILE__
    int         line;
    const char* function;  // Static string from __func__
};

/**
 * @brief Interface for log output destinations
 */
class ILogOutput {
public:
    virtual ~ILogOutput() = default;
    virtual void write(const LogEntry& entry) = 0;
};

/**
 * @brief Console output: INFO+ to stdout, WARN+ to stderr
 *
 * When attached to a terminal, optional ANSI color codes may be applied.
 */
class ConsoleLogOutput : public ILogOutput {
public:
    void write(const LogEntry& entry) override;
};

/**
 * @brief File output: all levels written to a single file
 *
 * Simple append mode; no rotation. Suitable for embedded systems that
 * restart on deploy.
 */
class FileLogOutput : public ILogOutput {
public:
    explicit FileLogOutput(const std::string& path);
    ~FileLogOutput() override;

    void write(const LogEntry& entry) override;

private:
    std::FILE* file_;
};

/**
 * @brief Singleton logger
 *
 * Default construction installs a ConsoleLogOutput at INFO level.
 * Main() calls setOutput()/addOutput() to configure at startup.
 */
class Logger {
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
    void log(LogLevel level, const char* tag, const char* file, int line,
             const char* function, const char* message) const;

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
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    mutable std::mutex mutex_;
    std::atomic<int> level_;
    std::vector<std::unique_ptr<ILogOutput>> outputs_;
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

#define WINDMI_LOG(level, tag, fmt, ...) \
    do { \
        if (windmi::Logger::instance().shouldLog(level)) { \
            char _logbuf[512]; \
            snprintf(_logbuf, sizeof(_logbuf), fmt, ##__VA_ARGS__); \
            windmi::Logger::instance().log(level, tag, __FILE__, __LINE__, \
                                           __func__, _logbuf); \
        } \
    } while (0)

#define WINDMI_LOG_TRACE(tag, fmt, ...)  WINDMI_LOG(windmi::LogLevel::TRACE, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_DEBUG(tag, fmt, ...)  WINDMI_LOG(windmi::LogLevel::DEBUG, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_INFO(tag, fmt, ...)   WINDMI_LOG(windmi::LogLevel::INFO, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_WARN(tag, fmt, ...)   WINDMI_LOG(windmi::LogLevel::WARN, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_ERROR(tag, fmt, ...)  WINDMI_LOG(windmi::LogLevel::ERROR, tag, fmt, ##__VA_ARGS__)
#define WINDMI_LOG_FATAL(tag, fmt, ...)  WINDMI_LOG(windmi::LogLevel::FATAL, tag, fmt, ##__VA_ARGS__)

#endif // WINDMI_UTILS_LOGGER_HPP