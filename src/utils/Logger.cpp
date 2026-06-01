/**
 * @file utils/Logger.cpp
 * @brief Logger singleton + ConsoleLogOutput + FileLogOutput implementation
 */

#include "utils/Logger.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace windmi {

// ──────────────────────────────────────────────────────────────────
// Logger singleton
// ──────────────────────────────────────────────────────────────────

Logger::Logger()
    : level_(static_cast<int>(LogLevel::INFO))
{
    // Install default console output at INFO level
    outputs_.push_back(std::make_unique<ConsoleLogOutput>());
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::setLevel(LogLevel level) {
    level_.store(static_cast<int>(level), std::memory_order_release);
}

bool Logger::shouldLog(LogLevel level) const {
    return static_cast<int>(level) >= level_.load(std::memory_order_acquire);
}

void Logger::addOutput(std::unique_ptr<ILogOutput> output) {
    std::lock_guard<std::mutex> lock(mutex_);
    outputs_.push_back(std::move(output));
}

void Logger::setOutput(std::unique_ptr<ILogOutput> output) {
    std::lock_guard<std::mutex> lock(mutex_);
    outputs_.clear();
    outputs_.push_back(std::move(output));
}

void Logger::log(LogLevel level, const char* tag, const char* file, int line,
                 const char* function, const char* message) const
{
    LogEntry entry;
    entry.timestamp  = std::chrono::system_clock::now();
    entry.level      = level;
    entry.tag        = tag;
    entry.message    = message;
    entry.file       = file;
    entry.line       = line;
    entry.function   = function;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& out : outputs_) {
        out->write(entry);
    }
}

std::string Logger::formatLevel(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
    }
    return "     "; // fallback, shouldn't happen
}

std::string Logger::formatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  tp.time_since_epoch()) % 1000;

    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);

    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
             static_cast<int>(ms.count()));
    return buf;
}

// ──────────────────────────────────────────────────────────────────
// ConsoleLogOutput
// ──────────────────────────────────────────────────────────────────

void ConsoleLogOutput::write(const LogEntry& entry) {
    std::string ts   = Logger::formatTimestamp(entry.timestamp);
    std::string lvl  = Logger::instance().formatLevel(entry.level);

    // Determine output stream
    FILE* stream = stdout;
    if (entry.level >= LogLevel::WARN) {
        stream = stderr;
    }

    // Format: [timestamp] [LEVEL ] [TAG      ] message (file:line)
    fprintf(stream, "[%s] [%-5s] [%-9s] %s (%s:%d)\n",
            ts.c_str(), lvl.c_str(), entry.tag,
            entry.message.c_str(), entry.file, entry.line);
}

// ──────────────────────────────────────────────────────────────────
// FileLogOutput
// ──────────────────────────────────────────────────────────────────

FileLogOutput::FileLogOutput(const std::string& path)
    : file_(std::fopen(path.c_str(), "a"))
{
    if (!file_) {
        // If we can't open the file, just ignore — log to console only
    }
}

FileLogOutput::~FileLogOutput() {
    if (file_) {
        std::fclose(file_);
    }
}

void FileLogOutput::write(const LogEntry& entry) {
    if (!file_) return;

    std::string ts   = Logger::formatTimestamp(entry.timestamp);
    std::string lvl  = Logger::instance().formatLevel(entry.level);

    fprintf(file_, "[%s] [%-5s] [%-9s] %s (%s:%d)\n",
            ts.c_str(), lvl.c_str(), entry.tag,
            entry.message.c_str(), entry.file, entry.line);

    // Flush after each write to avoid losing entries on crash
    std::fflush(file_);
}

} // namespace windmi
