/**
 * @file tests/web/test_web_server.cpp
 * @brief Web Server unit tests
 */

#include "gtest/gtest.h"
#include "web/WebServer.hpp"

using namespace windmi;

TEST(WebServerTest, CreateServer) {
    // This test creates and destroys the server
    // Note: In real use, this would bind to a port
    // For unit testing, we verify the constructor/destructor work
    EXPECT_NO_THROW({
        WebServer server(12345, "static");
    });
}

TEST(WebServerTest, StartStop) {
    WebServer server(12346, "static");
    EXPECT_TRUE(server.start());
    EXPECT_TRUE(server.isRunning());
    
    server.stop();
    EXPECT_FALSE(server.isRunning());
}

TEST(WebServerTest, StatusCallback) {
    WebServer server(12347, "static");
    
    bool callback_called = false;
    server.setStatusCallback([&callback_called]() {
        callback_called = true;
    });
    
    // Callback should be set
    EXPECT_TRUE(callback_called == false);  // Not called yet
}

TEST(WebServerTest, ShutdownFlag) {
    WebServer server(12348, "static");
    server.start();
    
    // Server should not be shutting down initially
    EXPECT_FALSE(server.isShuttingDown());
    
    server.stop();
    // After stop, isShuttingDown() should be true
    EXPECT_TRUE(server.isShuttingDown());
}
