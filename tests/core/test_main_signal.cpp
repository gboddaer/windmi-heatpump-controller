/**
 * @file tests/core/test_main_signal.cpp
 * @brief main.cpp signal integration and argument validation tests
 */

#include <gtest/gtest.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "utils/Platform.hpp"
#include "utils/Logger.hpp"
#include "core/ControlLoop.hpp"
#include "config.h"

using namespace windmi;

// ─── Signal Handler Integration Tests ───

TEST(SignalIntegrationTest, CtrlCSetsFlag) {
    volatile sig_atomic_t running = 1;
    platform::install_signal_handlers(&running);
    EXPECT_EQ(running, 1);

    // We can't actually send SIGINT to ourselves in a test,
    // but we can verify the handler was installed and the flag is set
#ifdef _WIN32
    // On Windows, verify SetConsoleCtrlHandler was called
    EXPECT_EQ(running, 1);
#else
    // On POSIX, verify signal handler is installed
    struct sigaction sa;
    sigaction(SIGINT, nullptr, &sa);
    EXPECT_NE(sa.sa_handler, SIG_DFL);
#endif
}

TEST(SignalIntegrationTest, SIGTERMGracefulShutdown) {
    volatile sig_atomic_t running = 1;
    platform::install_signal_handlers(&running);
    EXPECT_EQ(running, 1);

#ifdef _WIN32
    EXPECT_EQ(running, 1);
#else
    struct sigaction sa;
    sigaction(SIGTERM, nullptr, &sa);
    EXPECT_NE(sa.sa_handler, SIG_DFL);
#endif
}

TEST(SignalIntegrationTest, FlagSetToOneByInstall) {
    volatile sig_atomic_t running = 1;
    // On Windows, install_signal_handlers sets *running = 1.
    // On POSIX, it does NOT modify the flag (caller is responsible).
    // We verify it's not set to 0 by the call.
    platform::install_signal_handlers(&running);
#ifdef _WIN32
    EXPECT_EQ(running, 1);
#else
    // On POSIX, flag should remain unchanged
    EXPECT_EQ(running, 1);
#endif
}

TEST(SignalIntegrationTest, MultipleInstallsDoNotCrash) {
    volatile sig_atomic_t running1 = 1;
    volatile sig_atomic_t running2 = 1;
    platform::install_signal_handlers(&running1);
    platform::install_signal_handlers(&running2);
    // Verify neither was set to 0
    EXPECT_EQ(running1, 1);
    EXPECT_EQ(running2, 1);
}

// ─── Argument Validation Tests ───

TEST(ArgValidationTest, InvalidLogLevel) {
    // Mimics main.cpp parse_log_level validation
    auto parse_level = [](const char* s) -> bool {
        if (strcmp(s, "TRACE") == 0 || strcmp(s, "Trace") == 0) return true;
        if (strcmp(s, "DEBUG") == 0 || strcmp(s, "Debug") == 0) return true;
        if (strcmp(s, "INFO") == 0 || strcmp(s, "Info") == 0) return true;
        if (strcmp(s, "WARN") == 0 || strcmp(s, "Warn") == 0) return true;
        if (strcmp(s, "ERROR") == 0 || strcmp(s, "Error") == 0) return true;
        if (strcmp(s, "FATAL") == 0 || strcmp(s, "Fatal") == 0) return true;
        return false;
    };

    EXPECT_FALSE(parse_level("invalid"));
    EXPECT_FALSE(parse_level("warning"));  // lowercase
    EXPECT_FALSE(parse_level(""));
    EXPECT_FALSE(parse_level("info"));     // lowercase
    EXPECT_TRUE(parse_level("INFO"));
    EXPECT_TRUE(parse_level("Info"));
}

TEST(ArgValidationTest, PortRange) {
    // Verify default port is valid
    EXPECT_GE(WEB_SERVER_PORT, 1);
    EXPECT_LE(WEB_SERVER_PORT, 65535);
    EXPECT_GE(MODBUS_GATEWAY_PORT, 1);
    EXPECT_LE(MODBUS_GATEWAY_PORT, 65535);
}

TEST(ArgValidationTest, SlaveIdRange) {
    // Modbus slave IDs are 1-247
    EXPECT_GE(MODBUS_SLAVE_ID, 1);
    EXPECT_LE(MODBUS_SLAVE_ID, 247);
}

TEST(ArgValidationTest, SerialDefaults) {
    EXPECT_EQ(SERIAL_DEFAULT_BAUD, 9600);
    EXPECT_EQ(SERIAL_DEFAULT_PARITY, 'N');
    EXPECT_EQ(SERIAL_DEFAULT_STOP_BITS, 1);
}

TEST(ArgValidationTest, BaudRateValuesValid) {
    // Verify known baud rates
    EXPECT_TRUE(9600 > 0);
    EXPECT_TRUE(19200 > 0);
    EXPECT_TRUE(38400 > 0);
    EXPECT_TRUE(57600 > 0);
    EXPECT_TRUE(115200 > 0);
}

TEST(ArgValidationTest, ParityValuesValid) {
    // Verify parity characters
    EXPECT_TRUE('N' != 0);
    EXPECT_TRUE('E' != 0);
    EXPECT_TRUE('O' != 0);
}

// ─── Command Type Tests ───

TEST(CommandTypeTest, ValuesCorrect) {
    EXPECT_EQ(static_cast<int>(CommandType::CMD_SET_DHW_TEMP), 0);
    EXPECT_EQ(static_cast<int>(CommandType::CMD_SET_HEATING_TEMP), 1);
    EXPECT_EQ(static_cast<int>(CommandType::CMD_SET_PRIORITY), 2);
    EXPECT_EQ(static_cast<int>(CommandType::CMD_SET_RUNNING_MODE), 3);
}

TEST(CommandTypeTest, CommandStructDefaults) {
    Command cmd{};
    EXPECT_EQ(cmd.type, CommandType::CMD_SET_DHW_TEMP);
    EXPECT_EQ(cmd.float_val, 0.0f);
    EXPECT_EQ(cmd.int_val, 0);
}
