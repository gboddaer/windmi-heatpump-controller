/**
 * @file tests/core/test_selftest.cpp
 * @brief Selftest unit tests using mock IModbusClient
 */

#include "gtest/gtest.h"
#include "selftest.hpp"
#include "modbus/IModbusClient.hpp"
#include "config.h"

using namespace windmi;

// Mock IModbusClient for testing selftest_run
class MockModbusClient : public IModbusClient {
public:
    bool connect() override { return true; }
    void disconnect() override {}
    bool isConnected() const override { return true; }

    int16_t readRegister(uint16_t address) override {
        if (address == REG_DEVICE_TYPE) return 8;
        if (address == REG_HEATING_TARGET) return mHeatingTarget;
        if (address == REG_DHW_TARGET) return 500;
        if (address == REG_OUTDOOR_TEMP) return 120;
        if (address == REG_DHW_TANK_TEMP) return 450;
        return 0;
    }

    void writeRegister(uint16_t address, uint16_t value) override {
        if (address == REG_HEATING_TARGET) {
            mHeatingTarget = static_cast<int16_t>(value);
        }
    }

    void flushBuffer() override {}
    std::string getLastError() const override { return ""; }

    int16_t mHeatingTarget = 400;
};

// Mock that throws on all register accesses
class FailingMockClient : public IModbusClient {
public:
    bool connect() override { return true; }
    void disconnect() override {}
    bool isConnected() const override { return true; }

    int16_t readRegister(uint16_t) override {
        throw ModbusException("simulated failure");
    }

    void writeRegister(uint16_t, uint16_t) override {
        throw ModbusException("simulated failure");
    }

    void flushBuffer() override {}
    std::string getLastError() const override { return "simulated failure"; }
};

TEST(SelftestTest, NullClientReturnsAllFailed) {
    SelftestReport report = selftest_run(nullptr);
    EXPECT_EQ(report.total, 6);
    EXPECT_EQ(report.failed, 6);
    EXPECT_EQ(report.passed, 0);
    EXPECT_FALSE(report.all_critical_passed);
}

TEST(SelftestTest, AllTestsPassWithMock) {
    MockModbusClient mock;
    SelftestReport report = selftest_run(&mock);
    EXPECT_EQ(report.total, 6);
    EXPECT_EQ(report.failed, 0);
    EXPECT_EQ(report.passed, 6);
    EXPECT_TRUE(report.all_critical_passed);
}

TEST(SelftestTest, AllTestsFailWithFailingMock) {
    FailingMockClient mock;
    SelftestReport report = selftest_run(&mock);
    EXPECT_EQ(report.total, 6);
    EXPECT_EQ(report.failed, 6);
    EXPECT_EQ(report.passed, 0);
    EXPECT_FALSE(report.all_critical_passed);
}

TEST(SelftestTest, ResultsHaveCorrectAddresses) {
    MockModbusClient mock;
    SelftestReport report = selftest_run(&mock);
    EXPECT_EQ(report.results[0].address, REG_DEVICE_TYPE);
    EXPECT_EQ(report.results[1].address, REG_HEATING_TARGET);
    EXPECT_EQ(report.results[2].address, REG_DHW_TARGET);
    EXPECT_EQ(report.results[3].address, REG_OUTDOOR_TEMP);
    EXPECT_EQ(report.results[4].address, REG_DHW_TANK_TEMP);
    EXPECT_EQ(report.results[5].address, REG_HEATING_TARGET);
}

TEST(SelftestTest, WriteVerifyRestoresOriginal) {
    MockModbusClient mock;
    EXPECT_EQ(mock.mHeatingTarget, 400);  // Original value

    SelftestReport report = selftest_run(&mock);
    EXPECT_EQ(report.passed, 6);

    // After selftest, original value should be restored
    EXPECT_EQ(mock.mHeatingTarget, 400);
}
