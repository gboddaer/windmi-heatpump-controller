/**
 * @file src/main.cpp
 * @brief Main entry point for Windmi Controller
 *
 * Matches master branch main.c functionality:
 * - Instance lock via Platform abstraction
 * - Signal handling via Platform abstraction
 * - CLI: --ip, --port, --web, --log-level, --log-file, --selftest, --help
 * - SPSC queues for thread communication
 * - Startup: Modbus connect → ControlLoop → WebServer
 * - Shutdown: WebServer stop → ControlLoop stop → OFF mode write → lock release
 */

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <climits>

#include "config.h"
#include "core/ControlLoop.hpp"
#include "core/StatusMonitor.hpp"
#include "modbus/ModbusClient.hpp"
#include "modbus/SimulatedModbusClient.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include "utils/Platform.hpp"
#include "web/WebServer.hpp"
#include "crc16.h"

extern "C" {
#include "selftest.h"
}

// config.h defines (MODBUS_SLAVE_ID etc.) are in the extern "C" block
// inside config.h itself, so they are available here.

static volatile sig_atomic_t g_running = 1;

// Global objects for signal handler access
static windmi::WebServer* g_web_server = nullptr;
static windmi::ControlLoop* g_control_loop = nullptr;

/**
 * Resolve the static files directory.
 *
 * If @p dir exists as-is, return it.
 * Otherwise, try relative to the executable directory (useful when running
 * from a build/ subdirectory).
 */
static std::string resolve_static_dir(const std::string& dir) {
    return windmi::platform::resolve_static_dir(dir);
}

// Acquire exclusive lock to prevent multiple instances
static int acquire_lock(bool force) {
    return windmi::platform::acquire_instance_lock(force) ? 0 : -1;
}

// Release instance lock
static void release_lock() {
    windmi::platform::release_instance_lock();
}

// Register atexit handler for lock release
static void atexit_release_lock() {
    windmi::platform::release_instance_lock();
}

/**
 * Parse command line arguments.
 * Returns true if parsing succeeded, false otherwise.
 */
static bool parse_args(int argc, char* argv[],
                      std::string& modbus_ip, int& modbus_port,
                      std::string& web_ip, int& web_port,
                      std::string& static_dir,
                      windmi::LogLevel& log_level,
                      std::string& log_file,
                      bool& demo_mode,
                      bool& run_selftest,
                      bool& force_override) {
    modbus_ip = "127.0.0.1";
    modbus_port = 502;
    web_ip = WEB_SERVER_IP;
    web_port = WEB_SERVER_PORT;
    static_dir = "./static";
    log_level = windmi::LogLevel::INFO;
    log_file = "";
    demo_mode = false;
    run_selftest = false;
    force_override = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
            modbus_ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            modbus_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--web") == 0 && i + 1 < argc) {
            web_ip = argv[++i];
        } else if (strcmp(argv[i], "--web-port") == 0 && i + 1 < argc) {
            web_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--web-dir") == 0 && i + 1 < argc) {
            static_dir = argv[++i];
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            const char* level_str = argv[++i];
            if (strcmp(level_str, "TRACE") == 0) log_level = windmi::LogLevel::TRACE;
            else if (strcmp(level_str, "DEBUG") == 0) log_level = windmi::LogLevel::DEBUG;
            else if (strcmp(level_str, "INFO") == 0) log_level = windmi::LogLevel::INFO;
            else if (strcmp(level_str, "WARN") == 0) log_level = windmi::LogLevel::WARN;
            else if (strcmp(level_str, "ERROR") == 0) log_level = windmi::LogLevel::ERROR;
            else {
                fprintf(stderr, "Unknown log level: %s\n", level_str);
                return false;
            }
        } else if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else if (strcmp(argv[i], "--selftest") == 0) {
            run_selftest = true;
        } else if (strcmp(argv[i], "--demo") == 0) {
            demo_mode = true;
        } else if (strcmp(argv[i], "--force") == 0) {
            force_override = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --ip <ip>           Modbus gateway IP (default: 127.0.0.1)\n");
            printf("  --port <port>       Modbus gateway port (default: 502)\n");
            printf("  --web <ip>          Web server bind IP (default: %s)\n", WEB_SERVER_IP);
            printf("  --web-port <port>   Web server port (default: %d)\n", WEB_SERVER_PORT);
            printf("  --web-dir <dir>     Static files directory (default: ./static)\n");
            printf("  --log-level <level> Log level: TRACE, DEBUG, INFO, WARN, ERROR (default: INFO)\n");
            printf("  --log-file <file>   Log file path (default: stdout)\n");
            printf("  --selftest          Run self-test and exit\n");
            printf("  --demo              Demo mode with simulated device\n");
            printf("  --force             Override existing instance lock\n");
            printf("  --help              Show this help message\n");
            return true;
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    std::string modbus_ip;
    int modbus_port;
    std::string web_ip;
    int web_port;
    std::string static_dir;
    windmi::LogLevel log_level;
    std::string log_file;
    bool demo_mode;
    bool run_selftest;
    bool force_override;

    if (!parse_args(argc, argv, modbus_ip, modbus_port, web_ip, web_port,
                    static_dir, log_level, log_file, demo_mode, run_selftest, force_override)) {
        return 1;
    }

    if (run_selftest && demo_mode) {
        fprintf(stderr, "--selftest is not supported in demo mode\n");
        return 1;
    }

    // Initialize logger
    windmi::Logger& logger = windmi::Logger::instance();
    logger.setLevel(log_level);
    if (!log_file.empty()) {
        logger.setOutput(std::make_unique<windmi::FileLogOutput>(log_file));
    } else {
        logger.setOutput(std::make_unique<windmi::StdoutLogOutput>());
    }

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Starting Windmi Controller...");
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Modbus gateway: %s:%d", modbus_ip.c_str(), modbus_port);
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Web server: %s:%d", web_ip.c_str(), web_port);

    std::string resolved_static_dir = resolve_static_dir(static_dir);
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Static files: %s", resolved_static_dir.c_str());

    // Create SPSC queues
    windmi::CmdQueue cmd_queue;
    windmi::StatusQueue status_queue;

    // Create Modbus client (interface pointer for demo mode support)
    std::unique_ptr<windmi::IModbusClient> modbus_client;

    // Demo mode: reject --selftest, use simulated client
    if (demo_mode) {
        modbus_client = std::make_unique<windmi::SimulatedModbusClient>();
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "DEMO MODE: using simulated Windmi device, no Modbus socket will be opened");
    } else {
        modbus_client = std::make_unique<windmi::ModbusClient>(modbus_ip.c_str(), modbus_port, MODBUS_SLAVE_ID);
    }

    // Install signal handlers before acquiring lock
    windmi::platform::install_signal_handlers(&g_running);

    // Self-test mode: run self-test and exit (only for non-demo)
    if (run_selftest) {
        if (!acquire_lock(force_override)) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to acquire lock for self-test");
            return 1;
        }

        // Cast is safe here: we only reach here in non-demo mode
        auto* real_client = dynamic_cast<windmi::ModbusClient*>(modbus_client.get());
        if (!real_client || !real_client->connect()) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to connect to Modbus for self-test");
            release_lock();
            return 1;
        }

        // Get C client for selftest_run (which takes modbus_client_t*)
        modbus_client_t* c_client = static_cast<modbus_client_t*>(real_client->getCClient());
        selftest_report_t selftest_result = selftest_run(c_client);
        selftest_print_report(&selftest_result);
        WINDMI_LOG_INFO(LOG_TAG_SELFTEST, "%d/%d registers passed", selftest_result.passed, selftest_result.total);
        if (selftest_result.all_critical_passed) {
            WINDMI_LOG_INFO(LOG_TAG_SELFTEST, "Self-test PASSED");
        } else {
            WINDMI_LOG_WARN(LOG_TAG_SELFTEST, "Self-test FAILED");
        }
        real_client->disconnect();
        release_lock();
        return selftest_result.all_critical_passed ? 0 : 1;
    }

    // Acquire lock (no-op in demo mode, but we still call it for consistency)
    if (!acquire_lock(force_override)) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to acquire instance lock");
        return 1;
    }

    // Register atexit handler for lock release
    atexit(atexit_release_lock);

    // Connect to Modbus gateway (no-op for simulated client)
    if (!modbus_client->connect()) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to connect to Modbus gateway");
        release_lock();
        return 1;
    }

    // Create and start control loop (pass Modbus client + queues)
    g_control_loop = new windmi::ControlLoop();
    if (!g_control_loop->start(modbus_client.get(), &cmd_queue, &status_queue)) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to start control loop");
        delete g_control_loop;
        g_control_loop = nullptr;
        release_lock();
        return 1;
    }

    // Create and init web server (pass queues)
    try {
        g_web_server = new windmi::WebServer(web_port, resolved_static_dir, &cmd_queue, &status_queue,
                                             []() {
                                                 if (g_control_loop) {
                                                     g_control_loop->kick();
                                                 }
                                             });
    } catch (const std::exception& e) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to initialize web server: %s", e.what());
        g_control_loop->stop();
        delete g_control_loop;
        g_control_loop = nullptr;
        release_lock();
        return 1;
    }

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Server started. Press Ctrl+C to stop.");

    // Poll web server until a signal requests shutdown. The signal handler only
    // flips g_running; all C++ calls happen here outside signal context.
    while (g_running) {
        g_web_server->poll(100);
    }
    g_web_server->stop();

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Shutting down...");

    // Stop control loop
    g_control_loop->stop();
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Control loop stopped");

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Closing Modbus connection...");
    modbus_client->disconnect();

    // Drain/quiet period
    windmi::platform::sleep_ms(150);  // 150ms
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Drain period complete");

    // Write OFF mode via dedicated shutdown client (skip in demo mode)
    if (!demo_mode) {
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Writing OFF mode via dedicated client...");
        bool off_written = false;
        try {
            windmi::ModbusClient shutdown_client(modbus_ip.c_str(), modbus_port, MODBUS_SLAVE_ID);

            for (int attempt = 1; attempt <= 3; attempt++) {
                if (shutdown_client.connect()) {
                    shutdown_client.flushBuffer();

                    try {
                        shutdown_client.writeRegister(REG_RUNNING_MODE, 0);
                        WINDMI_LOG_INFO(LOG_TAG_MAIN, "OFF write OK (attempt %d)", attempt);
                        off_written = true;
                        shutdown_client.disconnect();
                        break;
                    } catch (const windmi::ModbusException&) {
                        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "OFF write failed (attempt %d)", attempt);
                        shutdown_client.disconnect();
                    }
                } else {
                    WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Connect failed (attempt %d)", attempt);
                }

                if (attempt < 3) {
                    windmi::platform::sleep_ms(100);  // 100ms retry delay
                }
            }
        } catch (const std::exception& e) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Exception creating shutdown client: %s", e.what());
        }

        if (!off_written) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "OFF mode write failed after 3 attempts");
        }
    } else {
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "DEMO MODE: skipping real OFF write");
    }

    // Cleanup
    delete g_web_server;
    g_web_server = nullptr;

    delete g_control_loop;
    g_control_loop = nullptr;

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Goodbye!");

    release_lock();
    return 0;
}
