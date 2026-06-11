/**
 * @file src/main.cpp
 * @brief Main entry point for Windmi Controller
 */

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <memory>
#include <string>

#include "config.h"
#include "core/ControlLoop.hpp"
#include "core/StatusMonitor.hpp"
#include "modbus/ModbusClient.hpp"
#include "modbus/ModbusSerialClient.hpp"
#include "modbus/SimulatedModbusClient.hpp"
#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include "utils/Platform.hpp"
#include "web/WebServer.hpp"
#include "crc16.h"

#include "selftest.hpp"

static volatile sig_atomic_t g_running = 1;

static windmi::WebServer* g_web_server = nullptr;
static windmi::ControlLoop* g_control_loop = nullptr;

static std::string resolve_static_dir(const std::string& dir) {
    return windmi::platform::resolve_static_dir(dir);
}

static bool acquire_lock(bool force) {
    return windmi::platform::acquire_instance_lock(force);
}

static void release_lock() {
    windmi::platform::release_instance_lock();
}

static void atexit_release_lock() {
    windmi::platform::release_instance_lock();
}

static bool parse_log_level(const char* level_str, windmi::LogLevel& out) {
    if (strcmp(level_str, "TRACE") == 0 || strcmp(level_str, "Trace") == 0) {
        out = windmi::LogLevel::Trace;
    } else if (strcmp(level_str, "DEBUG") == 0 || strcmp(level_str, "Debug") == 0) {
        out = windmi::LogLevel::Debug;
    } else if (strcmp(level_str, "INFO") == 0 || strcmp(level_str, "Info") == 0) {
        out = windmi::LogLevel::Info;
    } else if (strcmp(level_str, "WARN") == 0 || strcmp(level_str, "Warn") == 0) {
        out = windmi::LogLevel::Warn;
    } else if (strcmp(level_str, "ERROR") == 0 || strcmp(level_str, "Error") == 0) {
        out = windmi::LogLevel::Error;
    } else if (strcmp(level_str, "FATAL") == 0 || strcmp(level_str, "Fatal") == 0) {
        out = windmi::LogLevel::Fatal;
    } else {
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    const char* modbus_ip = MODBUS_GATEWAY_IP;
    int modbus_port = MODBUS_GATEWAY_PORT;
    std::string serial_device;
    int baud_rate = SERIAL_DEFAULT_BAUD;
    char parity = SERIAL_DEFAULT_PARITY;
    int stop_bits = SERIAL_DEFAULT_STOP_BITS;
    bool rs485_enabled = false;
    bool serial_config_specified = false;
    int web_port = WEB_SERVER_PORT;
    std::string static_dir = "static";
    std::string log_file_path;
    windmi::LogLevel log_level = windmi::LogLevel::Info;
    bool run_selftest = false;
    bool demo_mode = false;
    bool force_lock = false;
    std::string config_path;
    bool ip_specified = false;
    bool port_specified = false;
    bool serial_specified = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -i, --ip <address>      Modbus gateway IP (default: %s)\n", MODBUS_GATEWAY_IP);
            printf("  -p, --port <port>       Modbus gateway port (default: %d)\n", MODBUS_GATEWAY_PORT);
            printf("  -w, --web <port>        Web server HTTP port (default: %d, demo: 10000)\n", WEB_SERVER_PORT);
            printf("  -t, --static-dir <dir>  Static files directory (default: static)\n");
            printf("  -l, --log-level <lvl>   Log level: TRACE,DEBUG,INFO,WARN,ERROR,FATAL\n");
            printf("  -o, --log-file <path>   Log to file (in addition to console)\n");
            printf("  -f, --force             Force start even if lock is held by stale process\n");
            printf("  -s, --selftest          Run self-test and exit\n");
            printf("  -d, --demo              Run in demo mode with simulated Windmi device\n");
            printf("      --serial <device>   Serial device for Modbus RTU (e.g., /dev/ttyUSB0 or COM3)\n");
            printf("      --baud <rate>       Baud rate (default: %d)\n", SERIAL_DEFAULT_BAUD);
            printf("      --parity <N|E|O>    Parity: N(none), E(even), O(odd)\n");
            printf("      --stop-bits <1|2>   Stop bits: 1 or 2\n");
            printf("      --rs485             Enable RS-485 direction control\n");
            printf("      --config <path>     Settings file path (default: ~/.windmi/settings.ini)\n");
            return 0;
        } else if ((strcmp(argv[i], "--ip") == 0 || strcmp(argv[i], "-i") == 0) && i + 1 < argc) {
            modbus_ip = argv[++i];
            ip_specified = true;
        } else if ((strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0) && i + 1 < argc) {
            modbus_port = atoi(argv[++i]);
            port_specified = true;
        } else if ((strcmp(argv[i], "--web") == 0 || strcmp(argv[i], "-w") == 0) && i + 1 < argc) {
            web_port = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--static-dir") == 0 || strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--web-dir") == 0) && i + 1 < argc) {
            static_dir = argv[++i];
        } else if ((strcmp(argv[i], "--log-level") == 0 || strcmp(argv[i], "-l") == 0) && i + 1 < argc) {
            if (!parse_log_level(argv[++i], log_level)) {
                fprintf(stderr, "Unknown log level (use TRACE,DEBUG,INFO,WARN,ERROR,FATAL)\n");
                return 1;
            }
        } else if ((strcmp(argv[i], "--log-file") == 0 || strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            log_file_path = argv[++i];
        } else if (strcmp(argv[i], "--selftest") == 0 || strcmp(argv[i], "-s") == 0) {
            run_selftest = true;
        } else if (strcmp(argv[i], "--demo") == 0 || strcmp(argv[i], "-d") == 0) {
            demo_mode = true;
        } else if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
            force_lock = true;
        } else if (strcmp(argv[i], "--serial") == 0 && i + 1 < argc) {
            serial_device = argv[++i];
            serial_specified = true;
            serial_config_specified = true;
        } else if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud_rate = atoi(argv[++i]);
            serial_config_specified = true;
        } else if (strcmp(argv[i], "--parity") == 0 && i + 1 < argc) {
            const char* p = argv[++i];
            if (strlen(p) != 1 || (p[0] != 'N' && p[0] != 'E' && p[0] != 'O' && p[0] != 'n' && p[0] != 'e' && p[0] != 'o')) {
                fprintf(stderr, "Invalid parity: %s (must be N, E, or O)\n", p);
                return 1;
            }
            parity = static_cast<char>(toupper(static_cast<unsigned char>(p[0])));
            serial_config_specified = true;
        } else if (strcmp(argv[i], "--stop-bits") == 0 && i + 1 < argc) {
            stop_bits = atoi(argv[++i]);
            if (stop_bits != 1 && stop_bits != 2) {
                fprintf(stderr, "Invalid stop bits: %d (must be 1 or 2)\n", stop_bits);
                return 1;
            }
            serial_config_specified = true;
        } else if (strcmp(argv[i], "--rs485") == 0) {
            rs485_enabled = true;
            serial_config_specified = true;
        } else if ((strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            config_path = argv[++i];
        } else {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Unknown option: %s", argv[i]);
            WINDMI_LOG_INFO(LOG_TAG_MAIN, "Use --help for usage information");
            return 1;
        }
    }

    windmi::Logger& logger = windmi::Logger::instance();
    logger.setLevel(log_level);
    if (!log_file_path.empty()) {
        logger.addOutput(std::make_unique<windmi::FileLogOutput>(log_file_path));
    }

    if (serial_specified) {
        if (ip_specified || port_specified) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "--serial is mutually exclusive with --ip and --port");
            return 1;
        }
        if (demo_mode) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "--serial is mutually exclusive with --demo");
            return 1;
        }
    }

    if (serial_config_specified && !serial_specified) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "--baud, --parity, --stop-bits, and --rs485 require --serial");
        return 1;
    }

    if (!acquire_lock(force_lock)) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to acquire instance lock");
        return 1;
    }
    atexit(atexit_release_lock);

    windmi::platform::install_signal_handlers(&g_running);

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Rotenso Windmi Controller");

    std::unique_ptr<windmi::IModbusClient> modbus_client;
    if (demo_mode) {
        modbus_client = std::make_unique<windmi::SimulatedModbusClient>();
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "DEMO MODE: using simulated Windmi device");
        if (web_port == WEB_SERVER_PORT) {
            web_port = 10000;
        }
    } else if (!serial_device.empty()) {
        modbus_client = std::make_unique<windmi::ModbusSerialClient>(serial_device, baud_rate, parity, stop_bits, rs485_enabled, MODBUS_SLAVE_ID);
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Serial Modbus RTU: %s @ %d %c%d", serial_device.c_str(), baud_rate, parity, stop_bits);
    } else {
        modbus_client = std::make_unique<windmi::ModbusClient>(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Modbus gateway: %s:%d", modbus_ip, modbus_port);
    }

    // Connect for selftest
    bool connected_for_setup = false;
    if (run_selftest) {
        if (!modbus_client->connect()) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to connect for setup");
            release_lock();
            return 1;
        }
        connected_for_setup = true;
    }

    if (run_selftest) {
        windmi::SelftestReport report = windmi::selftest_run(modbus_client.get());
        windmi::selftest_print_report(report);
        WINDMI_LOG_INFO(LOG_TAG_SELFTEST, "%d/%d registers passed", report.passed, report.total);
        if (report.all_critical_passed) {
            WINDMI_LOG_INFO(LOG_TAG_SELFTEST, "Self-test PASSED");
        } else {
            WINDMI_LOG_WARN(LOG_TAG_SELFTEST, "Self-test FAILED");
        }
        modbus_client->disconnect();
        release_lock();
        return report.all_critical_passed ? 0 : 1;
    }

    // Normal operation - connect if not already connected
    if (connected_for_setup) {
        modbus_client->disconnect();
    }

    if (!modbus_client->connect()) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to connect to Modbus gateway");
        release_lock();
        return 1;
    }

    std::string resolved_static_dir = resolve_static_dir(static_dir);
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Web server port: %d", web_port);
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Static files: %s", resolved_static_dir.c_str());

    // Load persistent settings
    windmi::Config config;
    if (config_path.empty()) {
        std::string home = windmi::platform::get_home_dir();
        config_path = home + "/.windmi/settings.ini";
    }
    if (config.loadFromFile(config_path)) {
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Loaded settings from %s", config_path.c_str());
    } else {
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "No settings file at %s, using defaults", config_path.c_str());
    }
    int init_working_mode = config.getInt("ui.working_mode", 3);
    std::string init_priority = config.getString("ui.priority", "dhw");
    float init_dhw_target = static_cast<float>(config.getDouble("ui.dhw_target", 45.0));
    float init_heating_target = static_cast<float>(config.getDouble("ui.heating_target", 40.0));

    windmi::CmdQueue cmd_queue;
    windmi::StatusQueue status_queue;

    g_control_loop = new windmi::ControlLoop();
    g_control_loop->setInitialSettings(init_working_mode, init_priority,
                                       init_dhw_target, init_heating_target);
    g_control_loop->setSettingsCallback([&config, config_path](
        int working_mode, const std::string& priority,
        float dhw_target, float heating_target) {
            config.set("ui.working_mode", std::to_string(working_mode));
            config.set("ui.priority", priority);
            config.set("ui.dhw_target", std::to_string(dhw_target));
            config.set("ui.heating_target", std::to_string(heating_target));
            config.saveToFile(config_path);
        });
    if (!g_control_loop->start(modbus_client.get(), &cmd_queue, &status_queue)) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to start control loop");
        delete g_control_loop;
        g_control_loop = nullptr;
        release_lock();
        return 1;
    }

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
    while (g_running) {
        g_web_server->pollOnce(100);
    }
    g_web_server->stop();

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Shutting down...");
    g_control_loop->stop();
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Control loop stopped");

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Closing Modbus connection...");
    modbus_client->disconnect();

    windmi::platform::sleep_ms(150);
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Drain period complete");

    if (!demo_mode) {
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Writing OFF mode via dedicated client...");
        bool off_written = false;
        try {
            std::unique_ptr<windmi::IModbusClient> shutdown_client;
            if (!serial_device.empty()) {
                shutdown_client = std::make_unique<windmi::ModbusSerialClient>(serial_device, baud_rate, parity, stop_bits, rs485_enabled, MODBUS_SLAVE_ID);
                WINDMI_LOG_INFO(LOG_TAG_MAIN, "Shutdown: using serial client on %s", serial_device.c_str());
            } else {
                shutdown_client = std::make_unique<windmi::ModbusClient>(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
                WINDMI_LOG_INFO(LOG_TAG_MAIN, "Shutdown: using TCP client on %s:%d", modbus_ip, modbus_port);
            }

            for (int attempt = 1; attempt <= 3; attempt++) {
                if (shutdown_client->connect()) {
                    shutdown_client->flushBuffer();
                    try {
                        shutdown_client->writeRegister(REG_RUNNING_MODE, 0);
                        WINDMI_LOG_INFO(LOG_TAG_MAIN, "OFF write OK (attempt %d)", attempt);
                        off_written = true;
                        shutdown_client->disconnect();
                        break;
                    } catch (const windmi::ModbusException&) {
                        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "OFF write failed (attempt %d)", attempt);
                        shutdown_client->disconnect();
                    }
                } else {
                    WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Connect failed (attempt %d)", attempt);
                }
                if (attempt < 3) {
                    windmi::platform::sleep_ms(100);
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

    delete g_web_server;
    g_web_server = nullptr;
    delete g_control_loop;
    g_control_loop = nullptr;

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Goodbye!");
    release_lock();
    return 0;
}
