/**
 * @file utils/Logger.cpp
 * @brief Logger singleton + ConsoleLogOutput + FileLogOutput implementation
 */

#include "utils/Logger.hpp"
#include "utils/Platform.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace windmi {

// ──────────────────────────────────────────────────────────────────
// Logger singleton
// ──────────────────────────────────────────────────────────────────

Logger::Logger() : level_(static_cast<int>(LogLevel::Info))
{
  // Install default console output at INFO level
  outputs_.push_back(std::make_unique<ConsoleLogOutput>());
}

Logger::~Logger()
{
  // Mutex is cleaned up automatically by Mutex destructor
}

Logger& Logger::instance()
{
  static Logger inst;
  return inst;
}

void Logger::setLevel(LogLevel level)
{
  level_.store(static_cast<int>(level), std::memory_order_release);
}

bool Logger::shouldLog(LogLevel level) const
{
  return static_cast<int>(level) >= level_.load(std::memory_order_acquire);
}

void Logger::addOutput(std::unique_ptr<ILogOutput> output)
{
  LockGuard lock(mutex_);
  outputs_.push_back(std::move(output));
}

void Logger::setOutput(std::unique_ptr<ILogOutput> output)
{
  LockGuard lock(mutex_);
  outputs_.clear();
  outputs_.push_back(std::move(output));
}

void Logger::log(LogLevel level, const char* tag, const char* file, int line, const char* function,
                 const char* message) const
{
  LockGuard lock(mutex_);

  // Create log entry with timestamp
  LogEntry entry;
  entry.level = level;
  entry.tag = tag;
  entry.message = message;
  entry.file = file;
  entry.line = line;
  entry.function = function;
  entry.timestamp = std::chrono::system_clock::now();

  // Write to all outputs
  for (const auto& output : outputs_)
  {
    output->write(entry);
  }
}

// ──────────────────────────────────────────────────────────────────
// ConsoleLogOutput
// ──────────────────────────────────────────────────────────────────

void ConsoleLogOutput::write(const LogEntry& entry)
{
  std::string ts = Logger::formatTimestamp(entry.timestamp);
  std::string lvl = Logger::instance().formatLevel(entry.level);

  // Determine output stream
  FILE* stream = stdout;
  if (entry.level >= LogLevel::Warn)
  {
    stream = stderr;
  }

  // Format: [timestamp] [LEVEL ] [TAG      ] message (file:line)
  fprintf(stream, "[%s] [%-5s] [%-9s] %s (%s:%d)\n", ts.c_str(), lvl.c_str(), entry.tag,
          entry.message, entry.file, entry.line);
  std::fflush(stream);
}

// ──────────────────────────────────────────────────────────────────
// FileLogOutput
// ──────────────────────────────────────────────────────────────────

FileLogOutput::FileLogOutput(const std::string& path) : file_(std::fopen(path.c_str(), "a"))
{
  if (!file_)
  {
    // If we can't open the file, just ignore — log to console only
  }
}

FileLogOutput::~FileLogOutput()
{
  if (file_)
  {
    std::fclose(file_);
    file_ = nullptr;
  }
}

void FileLogOutput::write(const LogEntry& entry)
{
  if (!file_)
    return;

  std::string ts = Logger::formatTimestamp(entry.timestamp);
  std::string lvl = Logger::instance().formatLevel(entry.level);

  // Determine output stream (no color support for file)
  fprintf(file_, "[%s] [%-5s] [%-9s] %s (%s:%d)\n", ts.c_str(), lvl.c_str(), entry.tag,
          entry.message, entry.file, entry.line);
  std::fflush(file_);
}

// ──────────────────────────────────────────────────────────────────
// Logger helper methods
// ──────────────────────────────────────────────────────────────────

std::string Logger::formatTimestamp(const std::chrono::system_clock::time_point& tp)
{
  auto t = std::chrono::system_clock::to_time_t(tp);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;

  std::tm tm_buf{};
#ifdef _WIN32
  gmtime_s(&tm_buf, &t);
#else
  gmtime_r(&t, &tm_buf);
#endif

  char buf[64];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm_buf.tm_year + 1900,
           tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
           static_cast<int>(ms.count()));
  return buf;
}

std::string Logger::formatLevel(LogLevel level) const
{
  switch (level)
  {
  case LogLevel::Trace:
    return "Trace";
  case LogLevel::Debug:
    return "Debug";
  case LogLevel::Info:
    return "Info ";
  case LogLevel::Warn:
    return "Warn ";
  case LogLevel::Error:
    return "Error";
  case LogLevel::Fatal:
    return "Fatal";
  default:
    return "?????";
  }
}

} // namespace windmi
