/**
 * @file tests/utils/test_logger.cpp
 * @brief Unit tests for the structured Logger system
 */

#include <gtest/gtest.h>

#include "utils/Logger.hpp"
#include "utils/LoggerC.h"
#include "utils/LogTags.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using windmi::LogLevel;
using windmi::Logger;
using windmi::LogEntry;
using windmi::ILogOutput;
using windmi::ConsoleLogOutput;
using windmi::FileLogOutput;

// ─────────────────────────────────────────────────────────────────────────
// Test output that captures entries in memory
// ─────────────────────────────────────────────────────────────────────────
class TestLogOutput : public ILogOutput {
public:
    std::vector<LogEntry> entries;

    void write(const LogEntry& entry) override {
        entries.push_back(entry);
    }

    void clear() { entries.clear(); }

    std::string lastMessage() const {
        if (entries.empty()) return "";
        return entries.back().message;
    }

    std::string lastTag() const {
        if (entries.empty()) return "";
        return entries.back().tag;
    }

    int lastLine() const {
        if (entries.empty()) return -1;
        return entries.back().line;
    }
};

// ─────────────────────────────────────────────────────────────────────────
// Test fixture — resets logger before each test
// ─────────────────────────────────────────────────────────────────────────
class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Replace default console output with test output
        test_output_ = new TestLogOutput();
        Logger::instance().setOutput(std::unique_ptr<ILogOutput>(test_output_));
        Logger::instance().setLevel(LogLevel::Trace);  // Allow all levels
    }

    void TearDown() override {
        // Restore default console output
        Logger::instance().setOutput(std::make_unique<ConsoleLogOutput>());
        Logger::instance().setLevel(LogLevel::Info);
    }

    TestLogOutput* test_output_;  // Owned by Logger via unique_ptr
};

// ─────────────────────────────────────────────────────────────────────────
// Level filtering
// ─────────────────────────────────────────────────────────────────────────
TEST_F(LoggerTest, LevelFilteringSuppressesBelowThreshold) {
    Logger::instance().setLevel(LogLevel::Warn);
    test_output_->clear();

    WINDMI_LOG_TRACE(LOG_TAG_MAIN, "trace");
    WINDMI_LOG_DEBUG(LOG_TAG_MAIN, "debug");
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "info");
    WINDMI_LOG_WARN(LOG_TAG_MAIN, "warn");
    WINDMI_LOG_ERROR(LOG_TAG_MAIN, "error");

    // Only WARN and ERROR should be captured
    ASSERT_EQ(test_output_->entries.size(), 2u);
    EXPECT_EQ(test_output_->entries[0].level, LogLevel::Warn);
    EXPECT_EQ(test_output_->entries[1].level, LogLevel::Error);
}

TEST_F(LoggerTest, LevelFilteringAllowsAllAtTrace) {
    Logger::instance().setLevel(LogLevel::Trace);
    test_output_->clear();

    WINDMI_LOG_TRACE(LOG_TAG_MAIN, "trace");
    WINDMI_LOG_DEBUG(LOG_TAG_MAIN, "debug");
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "info");
    WINDMI_LOG_WARN(LOG_TAG_MAIN, "warn");
    WINDMI_LOG_ERROR(LOG_TAG_MAIN, "error");
    WINDMI_LOG_FATAL(LOG_TAG_MAIN, "fatal");

    EXPECT_EQ(test_output_->entries.size(), 6u);
}

TEST_F(LoggerTest, ShouldLogMatchesSetLevel) {
    Logger::instance().setLevel(LogLevel::Info);
    EXPECT_FALSE(Logger::instance().shouldLog(LogLevel::Trace));
    EXPECT_FALSE(Logger::instance().shouldLog(LogLevel::Debug));
    EXPECT_TRUE(Logger::instance().shouldLog(LogLevel::Info));
    EXPECT_TRUE(Logger::instance().shouldLog(LogLevel::Warn));
    EXPECT_TRUE(Logger::instance().shouldLog(LogLevel::Error));
    EXPECT_TRUE(Logger::instance().shouldLog(LogLevel::Fatal));
}

// ─────────────────────────────────────────────────────────────────────────
// Message content
// ─────────────────────────────────────────────────────────────────────────
TEST_F(LoggerTest, MessageFormatting) {
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Set mode to %d", 42);
    EXPECT_EQ(test_output_->lastMessage(), "Set mode to 42");
}

TEST_F(LoggerTest, TagIsPreserved) {
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "test");
    EXPECT_EQ(test_output_->lastTag(), "ControlLoop");
}

TEST_F(LoggerTest, LineNumberIsRecorded) {
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "test");  // line number should match this line
    EXPECT_GT(test_output_->lastLine(), 0);
}

TEST_F(LoggerTest, EmptyMessage) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "");
#pragma GCC diagnostic pop
    EXPECT_EQ(test_output_->lastMessage(), "");
}

TEST_F(LoggerTest, MultipleArgs) {
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Connected to %s:%d (mode %d)", "192.168.1.1", 502, 3);
    EXPECT_EQ(test_output_->lastMessage(), "Connected to 192.168.1.1:502 (mode 3)");
}

TEST_F(LoggerTest, CBridgeLogsFormattedMessage) {
    Logger::instance().setLevel(LogLevel::Info);

    EXPECT_TRUE(windmi_should_log(WINDMI_LOG_INFO));
    EXPECT_FALSE(windmi_should_log(WINDMI_LOG_DEBUG));

    windmi_log(WINDMI_LOG_INFO, LOG_TAG_MODBUS, "test.c", 123, "func",
               "C bridge value %d", 17);

    ASSERT_EQ(test_output_->entries.size(), 1u);
    EXPECT_EQ(test_output_->entries[0].level, LogLevel::Info);
    EXPECT_EQ(test_output_->entries[0].tag, std::string(LOG_TAG_MODBUS));
    EXPECT_STREQ(test_output_->entries[0].message, "C bridge value 17");
    EXPECT_EQ(test_output_->entries[0].file, std::string("test.c"));
    EXPECT_EQ(test_output_->entries[0].line, 123);
}

// ─────────────────────────────────────────────────────────────────────────
// Multiple outputs
// ─────────────────────────────────────────────────────────────────────────
TEST_F(LoggerTest, MultipleOutputs) {
    auto* second = new TestLogOutput();
    Logger::instance().addOutput(std::unique_ptr<ILogOutput>(second));

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "multi-output test");

    EXPECT_EQ(test_output_->entries.size(), 1u);
    EXPECT_EQ(second->entries.size(), 1u);
    EXPECT_EQ(test_output_->lastMessage(), "multi-output test");
}

TEST_F(LoggerTest, SetOutputReplacesPrevious) {
    auto* new_output = new TestLogOutput();
    Logger::instance().setOutput(std::make_unique<TestLogOutput>());
    // After setOutput, the old test_output_ is destroyed. Use new_output.
    Logger::instance().setOutput(std::unique_ptr<ILogOutput>(new_output));

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "replaced");

    // new_output should have received the message
    EXPECT_EQ(new_output->entries.size(), 1u);
    EXPECT_EQ(new_output->lastMessage(), "replaced");
}

// ─────────────────────────────────────────────────────────────────────────
// FileLogOutput
// ─────────────────────────────────────────────────────────────────────────
TEST_F(LoggerTest, FileLogOutputWritesToFile) {
    const char* test_file = "/tmp/windmi_test_logger.log";
    // Remove if exists
    std::remove(test_file);

    Logger::instance().addOutput(std::make_unique<FileLogOutput>(test_file));

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "file output test");

    // Flush is called after each write, so file should have content
    std::ifstream ifs(test_file);
    ASSERT_TRUE(ifs.is_open());
    std::string line;
    std::getline(ifs, line);
    EXPECT_NE(line.find("file output test"), std::string::npos);
    ifs.close();
    std::remove(test_file);
}

// ─────────────────────────────────────────────────────────────────────────
// Format helpers
// ─────────────────────────────────────────────────────────────────────────
TEST_F(LoggerTest, FormatLevelReturnsCorrectStrings) {
    EXPECT_EQ(Logger::instance().formatLevel(LogLevel::Trace), "Trace");
    EXPECT_EQ(Logger::instance().formatLevel(LogLevel::Debug), "Debug");
    EXPECT_EQ(Logger::instance().formatLevel(LogLevel::Info),  "Info ");
    EXPECT_EQ(Logger::instance().formatLevel(LogLevel::Warn),  "Warn ");
    EXPECT_EQ(Logger::instance().formatLevel(LogLevel::Error), "Error");
    EXPECT_EQ(Logger::instance().formatLevel(LogLevel::Fatal), "Fatal");
}

TEST_F(LoggerTest, FormatTimestampContainsYear) {
    auto now = std::chrono::system_clock::now();
    std::string ts = Logger::formatTimestamp(now);
    // Should contain 2025 or 2026 etc.
    EXPECT_NE(ts.find("20"), std::string::npos);
    // Should contain colons for HH:MM:SS
    EXPECT_NE(ts.find(':'), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────
// Thread safety (basic — no garbled output under concurrent writes)
// ─────────────────────────────────────────────────────────────────────────
TEST_F(LoggerTest, ConcurrentLoggingDoesNotCrash) {
    test_output_->clear();
    Logger::instance().setLevel(LogLevel::Info);

    const int num_threads = 4;
    const int messages_per_thread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([t]() {
            for (int i = 0; i < messages_per_thread; i++) {
                WINDMI_LOG_INFO(LOG_TAG_MAIN, "thread %d msg %d", t, i);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // No crash is the main assertion. Also check total count.
    EXPECT_EQ(test_output_->entries.size(),
              static_cast<size_t>(num_threads * messages_per_thread));
}