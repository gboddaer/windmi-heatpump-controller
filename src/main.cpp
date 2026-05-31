/**
 * @file src/main.cpp
 * @brief Main entry point for Windmi Controller
 */

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "config.h"
#include "core/ControlLoop.hpp"
#include "core/StatusMonitor.hpp"
#include "modbus/ModbusClient.hpp"
#include "web/WebServer.hpp"
#include "crc16.h"

// Global variables
static const char* MODBUS_IP = "192.168.123.10";
static int MODBUS_PORT = 8899;
static uint8_t MODBUS_SLAVE_ID = 1;
static int WEB_PORT = 8080;
static bool SELF_TEST = false;

static windmi::ControlLoop* g_control_loop = nullptr;
static windmi::WebServer* g_web_server = nullptr;

// Signal handler
static void signal_handler(int sig) {
    (void)sig;
    if (g_web_server) {
        g_web_server->stop();
    }
    if (g_control_loop) {
        g_control_loop->stop();
    }
}

// Parse command line arguments
static int parse_args(int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "w:s")) != -1) {
        switch (opt) {
        case 'w':
            WEB_PORT = atoi(optarg);
            break;
        case 's':
            SELF_TEST = true;
            break;
        default:
            fprintf(stderr, "Usage: %s [-w port] [-s]\n", argv[0]);
            return -1;
        }
    }
    return 0;
}

// Run self-test
static int run_selftest() {
    printf("[SelfTest] Starting self-test...\n");
    
    // Test basic components
    printf("[SelfTest] Testing CRC16...\n");
    uint8_t test_data[] = {0x01, 0x02, 0x03};
    uint16_t crc = crc16(test_data, sizeof(test_data));
    printf("[SelfTest] CRC16 result: 0x%04X\n", crc);
    
    printf("[SelfTest] Testing ModbusClient...\n");
    windmi::ModbusClient client(MODBUS_IP, MODBUS_PORT, MODBUS_SLAVE_ID);
    if (!client.connect()) {
        printf("[SelfTest] ModbusClient connect failed\n");
        return -1;
    }
    client.disconnect();
    
    printf("[SelfTest] All tests passed!\n");
    return 0;
}

int main(int argc, char* argv[]) {
    printf("[Main] Rotenso Windmi Controller\n");
    
    // Parse arguments
    if (parse_args(argc, argv) != 0) {
        return EXIT_FAILURE;
    }
    
    // Self-test mode
    if (SELF_TEST) {
        return run_selftest();
    }
    
    // Install signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, nullptr) != 0) {
        fprintf(stderr, "[Main] Failed to install signal handler\n");
        return EXIT_FAILURE;
    }
    
    // Create Modbus client
    std::unique_ptr<windmi::ModbusClient> modbus_client;
    try {
        modbus_client = std::make_unique<windmi::ModbusClient>(
            MODBUS_IP, MODBUS_PORT, MODBUS_SLAVE_ID);
        if (!modbus_client->connect()) {
            fprintf(stderr, "[Main] Failed to connect to Modbus\n");
            return EXIT_FAILURE;
        }
        printf("[Main] Modbus gateway: %s:%d (slave=%d)\n", 
               MODBUS_IP, MODBUS_PORT, MODBUS_SLAVE_ID);
    } catch (const std::exception& e) {
        fprintf(stderr, "[Main] Modbus exception: %s\n", e.what());
        return EXIT_FAILURE;
    }
    
    // Create status monitor and control loop
    windmi::StatusMonitor status_monitor;
    
    g_control_loop = new windmi::ControlLoop([&status_monitor](const windmi::StatusSnapshot& snap) {
        status_monitor.update(snap);
    });
    
    if (!g_control_loop->start()) {
        fprintf(stderr, "[Main] Failed to start control loop\n");
        delete g_control_loop;
        return EXIT_FAILURE;
    }
    printf("[Main] Control loop started\n");
    
    // Create web server
    g_web_server = new windmi::WebServer(WEB_PORT, "static");
    if (!g_web_server->start()) {
        fprintf(stderr, "[Main] Failed to start web server\n");
        delete g_web_server;
        g_control_loop->stop();
        delete g_control_loop;
        return EXIT_FAILURE;
    }
    printf("[Main] Web server: %s:%d\n", WEB_SERVER_IP, WEB_PORT);
    
    printf("[Main] Server started. Press Ctrl+C to stop.\n");
    
    // Wait for shutdown
    while (g_web_server->isRunning()) {
        usleep(100000);  // 100ms
    }
    
    // Shutdown sequence
    printf("[Main] Shutting down...\n");
    
    // Stop control loop first
    g_control_loop->stop();
    printf("[Shutdown] Control loop stopped\n");
    
    // Wait for control loop to join
    usleep(150000);  // 150ms drain
    printf("[Shutdown] Drain period complete\n");
    
    // Write OFF mode via dedicated client
    printf("[Shutdown] Writing OFF mode via dedicated client...\n");
    std::unique_ptr<windmi::ModbusClient> shutdown_client;
    bool off_written = false;
    
    try {
        shutdown_client = std::make_unique<windmi::ModbusClient>(
            MODBUS_IP, MODBUS_PORT, MODBUS_SLAVE_ID);
        
        for (int attempt = 1; attempt <= 3; attempt++) {
            if (shutdown_client->connect()) {
                shutdown_client->flushBuffer();
                
                try {
                    shutdown_client->writeRegister(REG_RUNNING_MODE, 0);
                    printf("[Shutdown] OFF write OK (attempt %d)\n", attempt);
                    off_written = true;
                    shutdown_client->disconnect();
                    break;
                } catch (const windmi::ModbusException& e) {
                    fprintf(stderr, "[Shutdown] Write error (attempt %d): %s\n", 
                            attempt, e.what());
                    shutdown_client->disconnect();
                }
            } else {
                fprintf(stderr, "[Shutdown] Connect failed (attempt %d)\n", attempt);
            }
            
            if (attempt < 3) {
                usleep(100000);  // 100ms retry delay
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[Shutdown] Exception: %s\n", e.what());
    }
    
    if (!off_written) {
        fprintf(stderr, "[Shutdown] OFF mode write failed\n");
    }
    
    delete g_web_server;
    g_web_server = nullptr;
    
    delete g_control_loop;
    g_control_loop = nullptr;
    
    printf("[Main] Goodbye!\n");
    
    return EXIT_SUCCESS;
}
