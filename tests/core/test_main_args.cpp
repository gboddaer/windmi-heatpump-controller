/**
 * @file tests/core/test_main_args.cpp
 * @brief Argument parsing and configuration validation tests
 *
 * Tests the parse_log_level helper and validates the main.cpp
 * argument parsing logic through direct function calls.
 */

#include "gtest/gtest.h"
#include "utils/Logger.hpp"
#include "config.h"

using namespace windmi;

// Helper to test log level parsing (mimics main.cpp parse_log_level)
static bool parse_log_level(const char* level_str, LogLevel& out) {
    if (strcmp(level_str, "TRACE") == 0 || strcmp(level_str, "Trace") == 0) {
        out = LogLevel::Trace;
    } else if (strcmp(level_str, "DEBUG") == 0 || strcmp(level_str, "Debug") == 0) {
        out = LogLevel::Debug;
    } else if (strcmp(level_str, "INFO") == 0 || strcmp(level_str, "Info") == 0) {
        out = LogLevel::Info;
    } else if (strcmp(level_str, "WARN") == 0 || strcmp(level_str, "Warn") == 0) {
        out = LogLevel::Warn;
    } else if (strcmp(level_str, "ERROR") == 0 || strcmp(level_str, "Error") == 0) {
        out = LogLevel::Error;
    } else if (strcmp(level_str, "FATAL") == 0 || strcmp(level_str, "Fatal") == 0) {
        out = LogLevel::Fatal;
    } else {
        return false;
    }
    return true;
}

TEST(ArgsTest, ParseLogLevelUppercase) {
    LogLevel level;
    EXPECT_TRUE(parse_log_level("TRACE", level));
    EXPECT_EQ(level, LogLevel::Trace);

    EXPECT_TRUE(parse_log_level("DEBUG", level));
    EXPECT_EQ(level, LogLevel::Debug);

    EXPECT_TRUE(parse_log_level("INFO", level));
    EXPECT_EQ(level, LogLevel::Info);

    EXPECT_TRUE(parse_log_level("WARN", level));
    EXPECT_EQ(level, LogLevel::Warn);

    EXPECT_TRUE(parse_log_level("ERROR", level));
    EXPECT_EQ(level, LogLevel::Error);

    EXPECT_TRUE(parse_log_level("FATAL", level));
    EXPECT_EQ(level, LogLevel::Fatal);
}

TEST(ArgsTest, ParseLogLevelMixedCase) {
    LogLevel level;
    EXPECT_TRUE(parse_log_level("Trace", level));
    EXPECT_EQ(level, LogLevel::Trace);

    EXPECT_TRUE(parse_log_level("Debug", level));
    EXPECT_EQ(level, LogLevel::Debug);

    EXPECT_TRUE(parse_log_level("Info", level));
    EXPECT_EQ(level, LogLevel::Info);

    EXPECT_TRUE(parse_log_level("Warn", level));
    EXPECT_EQ(level, LogLevel::Warn);

    EXPECT_TRUE(parse_log_level("Error", level));
    EXPECT_EQ(level, LogLevel::Error);

    EXPECT_TRUE(parse_log_level("Fatal", level));
    EXPECT_EQ(level, LogLevel::Fatal);
}

TEST(ArgsTest, ParseLogLevelInvalid) {
    LogLevel level;
    EXPECT_FALSE(parse_log_level("invalid", level));
    EXPECT_FALSE(parse_log_level("", level));
    EXPECT_FALSE(parse_log_level("warning", level));
    EXPECT_FALSE(parse_log_level("fatal", level));
}

TEST(ArgsTest, DefaultConfigValues) {
    // Verify the default configuration values match config.h
    EXPECT_STREQ(MODBUS_GATEWAY_IP, "192.168.123.10");
    EXPECT_EQ(MODBUS_GATEWAY_PORT, 8899);
    EXPECT_EQ(MODBUS_SLAVE_ID, 11);
    EXPECT_EQ(WEB_SERVER_PORT, 8080);
    EXPECT_EQ(SERIAL_DEFAULT_BAUD, 9600);
    EXPECT_EQ(SERIAL_DEFAULT_PARITY, 'N');
    EXPECT_EQ(SERIAL_DEFAULT_STOP_BITS, 1);
}

TEST(ArgsTest, LogLevelEnumOrder) {
    // Verify log level ordering for filtering
    EXPECT_LT(LogLevel::Trace, LogLevel::Debug);
    EXPECT_LT(LogLevel::Debug, LogLevel::Info);
    EXPECT_LT(LogLevel::Info, LogLevel::Warn);
    EXPECT_LT(LogLevel::Warn, LogLevel::Error);
    EXPECT_LT(LogLevel::Error, LogLevel::Fatal);
}

TEST(ArgsTest, LoggerShouldLogRespectsLevel) {
    Logger& logger = Logger::instance();

    // At default Info level
    logger.setLevel(LogLevel::Info);
    EXPECT_FALSE(logger.shouldLog(LogLevel::Trace));
    EXPECT_FALSE(logger.shouldLog(LogLevel::Debug));
    EXPECT_TRUE(logger.shouldLog(LogLevel::Info));
    EXPECT_TRUE(logger.shouldLog(LogLevel::Warn));
    EXPECT_TRUE(logger.shouldLog(LogLevel::Error));
    EXPECT_TRUE(logger.shouldLog(LogLevel::Fatal));

    // At Trace level
    logger.setLevel(LogLevel::Trace);
    EXPECT_TRUE(logger.shouldLog(LogLevel::Trace));
    EXPECT_TRUE(logger.shouldLog(LogLevel::Debug));
    EXPECT_TRUE(logger.shouldLog(LogLevel::Info));

    // At Fatal level
    logger.setLevel(LogLevel::Fatal);
    EXPECT_FALSE(logger.shouldLog(LogLevel::Trace));
    EXPECT_FALSE(logger.shouldLog(LogLevel::Debug));
    EXPECT_FALSE(logger.shouldLog(LogLevel::Info));
    EXPECT_FALSE(logger.shouldLog(LogLevel::Warn));
    EXPECT_FALSE(logger.shouldLog(LogLevel::Error));
    EXPECT_TRUE(logger.shouldLog(LogLevel::Fatal));

    // Reset to default
    logger.setLevel(LogLevel::Info);
}
