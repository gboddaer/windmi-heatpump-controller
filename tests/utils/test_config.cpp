/**
 * @file tests/utils/test_config.cpp
 * @brief Config unit tests
 */

#include "gtest/gtest.h"
#include "utils/Config.hpp"

using namespace windmi;

TEST(ConfigTest, DefaultValues) {
    Config config;
    EXPECT_EQ(config.getString("modbus.host", ""), "192.168.123.10");
    EXPECT_EQ(config.getInt("modbus.port", 0), 8899);
    EXPECT_EQ(config.getInt("modbus.slave_id", 0), 1);
}

TEST(ConfigTest, SetAndGetString) {
    Config config;
    config.set("test.key", "test_value");
    EXPECT_EQ(config.getString("test.key", ""), "test_value");
    EXPECT_EQ(config.getString("nonexistent.key", "default"), "default");
}

TEST(ConfigTest, SetAndGetInt) {
    Config config;
    config.set("test.int", "42");
    EXPECT_EQ(config.getInt("test.int", 0), 42);
    EXPECT_EQ(config.getInt("nonexistent", 99), 99);
}

TEST(ConfigTest, SetAndGetDouble) {
    Config config;
    config.set("test.double", "3.14");
    EXPECT_DOUBLE_EQ(config.getDouble("test.double", 0.0), 3.14);
}

TEST(ConfigTest, BooleanValues) {
    Config config;
    config.set("test.bool", "true");
    EXPECT_TRUE(config.getBool("test.bool", false));
    
    config.set("test.bool2", "1");
    EXPECT_TRUE(config.getBool("test.bool2", false));
    
    config.set("test.bool3", "false");
    EXPECT_FALSE(config.getBool("test.bool3", true));
}

TEST(ConfigTest, HasKey) {
    Config config;
    config.set("exists", "value");
    EXPECT_TRUE(config.hasKey("exists"));
    EXPECT_FALSE(config.hasKey("doesnotexist"));
}
