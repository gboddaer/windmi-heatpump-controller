/**
 * @file src/main.cpp
 * @brief Main entry point for Windmi Controller
 *
 * Matches master branch main.c functionality:
 * - Instance lock via flock()
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
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <climits>

#include "config.h"
#include "core/ControlLoop.hpp"
#include "core/StatusMonitor.hpp"
#include "modbus/ModbusClient.hpp"
#include "modbus/ModbusSerialClient.hpp"
#include "modbus/SimulatedModbusClient.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include "web/WebServer.hpp"
#include "crc16.h"

#include "selftest.hpp"

// config.h defines (MODBUS_SLAVE_ID etc.) are in the extern "C" block
// inside config.h itself, so they are available here.

#define LOCK_FILE "/tmp/windmi-controller.lock"

static volatile sig_atomic_t g_running = 1;
static int g_lock_fd = -1;

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
    struct stat st;
    if (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        // Normalize the path (resolve .., symlinks, etc.)
        char resolved[PATH_MAX];
        if (realpath(dir.c_str(), resolved)) {
            return resolved;
        }
        return dir;
    }

    // Try relative to the executable's directory
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        // Find last '/' to get directory
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            std::string candidate = std::string(exe_path) + "/" + dir;
            if (stat(candidate.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                char resolved[PATH_MAX];
                if (realpath(candidate.c_str(), resolved)) {
                    return resolved;
                }
                return candidate;
            }
            // Try one more level up (e.g. build/ -> project root)
            std::string candidate2 = std::string(exe_path) + "/../" + dir;
            if (stat(candidate2.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                char resolved[PATH_MAX];
                if (realpath(candidate2.c_str(), resolved)) {
                    return resolved;
                }
                return candidate2;
            }
        }
    }

    // Give up — return as-is; Mongoose will serve a directory listing
    WINDMI_LOG_WARN(LOG_TAG_MAIN, "Static directory not found: %s", dir.c_str());
    return dir;
}

// Signal handler
static void signal_handler(int sig) {
    (void)sig;
    // Async-signal-safe: only update sig_atomic_t state here.
    g_running = 0;
}

/**
 * Check if a process with the given PID is alive.
 * Uses /proc for Linux; always returns true on other systems.
 */
static bool is_pid_alive(pid_t pid) {
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", pid);
    struct stat st;
    return (stat(proc_path, &st) == 0);
}

// Acquire exclusive lock to prevent multiple instances
static int acquire_lock() {
    g_lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0644);
    if (g_lock_fd < 0) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to open lock file %s: %s", LOCK_FILE, strerror(errno));
        return -1;
    }

    // Set close-on-exec so child processes don't inherit the lock
    int flags = fcntl(g_lock_fd, F_GETFD);
    if (flags >= 0) {
        fcntl(g_lock_fd, F_SETFD, flags | FD_CLOEXEC);
    }

    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK) {
            // Read the PID from the lock file for diagnostics
            char pid_buf[32] = {};
            lseek(g_lock_fd, 0, SEEK_SET);
            ssize_t n = read(g_lock_fd, pid_buf, sizeof(pid_buf) - 1);
            (void)n;
            int existing_pid = atoi(pid_buf);
            if (existing_pid > 0 && is_pid_alive(existing_pid)) {
                WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Another instance is already running (PID %d)",
                        existing_pid);
            } else if (existing_pid > 0) {
                WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Lock held by stale process %d (not running)",
                        existing_pid);
                WINDMI_LOG_WARN(LOG_TAG_MAIN, "Remove %s or use --force to override", LOCK_FILE);
            } else {
                WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Another instance is already running");
            }
        } else {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to acquire lock: %s", strerror(errno));
        }
        close(g_lock_fd);
        g_lock_fd = -1;
        return -1;
    }

    // Write PID to lock file for diagnostics
    char pid_buf[32];
    int len = snprintf(pid_buf, sizeof(pid_buf), "%d\n", getpid());
    if (len > 0) {
        lseek(g_lock_fd, 0, SEEK_SET);
        write(g_lock_fd, pid_buf, static_cast<size_t>(len));
        ftruncate(g_lock_fd, len);
    }

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Lock acquired (PID: %d)", getpid());
    return 0;
}

static void release_lock() {
    if (g_lock_fd >= 0) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Lock released");
    }
}

// Safety net: release lock on unexpected exit (e.g., unhandled exception)
static void atexit_release_lock() {
    if (g_lock_fd >= 0) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
    }
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
    bool run_selftest = false;
    bool demo_mode = false;
    bool force_lock = false;
    bool ip_specified = false;
    bool port_specified = false;
    bool serial_specified = false;

    // Parse CLI arguments (long and short forms)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -i, --ip <address>  Modbus gateway IP (default: %s)\n", MODBUS_GATEWAY_IP);
            printf("  -p, --port <port>   Modbus gateway port (default: %d)\n", MODBUS_GATEWAY_PORT);
            printf("  -w, --web <port>    Web server HTTP port (default: %d, demo: 10000)\n", WEB_SERVER_PORT);
            printf("  -t, --static-dir <dir>  Static files directory (default: static)\n");
            printf("  -l, --log-level <lvl>  Log level: TRACE,DEBUG,INFO,WARN,ERROR,FATAL (default: INFO)\n");
            printf("  -o, --log-file <path>  Log to file (in addition to console)\n");
            printf("  -f, --force          Force start even if lock is held by stale process\n");
            printf("  -s, --selftest       Run self-test and exit\n");
            printf("  -d, --demo           Run in demo mode with simulated Windmi device\n");
            printf("  --serial <device>    Serial device for Modbus RTU (e.g., /dev/ttyUSB0)\n");
            printf("  --baud <rate>        Baud rate (default: %d)\n", SERIAL_DEFAULT_BAUD);
            printf("  --parity <N|E|O>     Parity: N(none), E(even), O(odd) (default: N)\n");
            printf("  --stop-bits <1|2>    Stop bits: 1 or 2 (default: 1)\n");
            printf("  --rs485              Enable RS-485 direction control\n");
            printf("  -h, --help           Show this help message\n");
            return 0;
        } else if ((strcmp(argv[i], "--ip") == 0 || strcmp(argv[i], "-i") == 0) && i + 1 < argc) {
            modbus_ip = argv[++i];
            ip_specified = true;
        } else if ((strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0) && i + 1 < argc) {
            modbus_port = atoi(argv[++i]);
            port_specified = true;
        } else if ((strcmp(argv[i], "--web") == 0 || strcmp(argv[i], "-w") == 0) && i + 1 < argc) {
            web_port = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--static-dir") == 0 || strcmp(argv[i], "-t") == 0) && i + 1 < argc) {
            static_dir = argv[++i];
        } else if ((strcmp(argv[i], "--log-level") == 0 || strcmp(argv[i], "-l") == 0) && i + 1 < argc) {
            const char* level_str = argv[++i];
            if (strcmp(level_str, "TRACE") == 0) {
                windmi::Logger::instance().setLevel(windmi::LogLevel::TRACE);
            } else if (strcmp(level_str, "DEBUG") == 0) {
                windmi::Logger::instance().setLevel(windmi::LogLevel::DEBUG);
            } else if (strcmp(level_str, "INFO") == 0) {
                windmi::Logger::instance().setLevel(windmi::LogLevel::INFO);
            } else if (strcmp(level_str, "WARN") == 0) {
                windmi::Logger::instance().setLevel(windmi::LogLevel::WARN);
            } else if (strcmp(level_str, "ERROR") == 0) {
                windmi::Logger::instance().setLevel(windmi::LogLevel::ERROR);
            } else if (strcmp(level_str, "FATAL") == 0) {
                windmi::Logger::instance().setLevel(windmi::LogLevel::FATAL);
            } else {
                fprintf(stderr, "Unknown log level: %s (use TRACE,DEBUG,INFO,WARN,ERROR,FATAL)\n", level_str);
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
            if (strlen(p) != 1 || (p[0] != 'N' && p[0] != 'E' && p[0] != 'O' &&
                                   p[0] != 'n' && p[0] != 'e' && p[0] != 'o')) {
                fprintf(stderr, "Invalid parity: %s (must be N, E, or O)\n", p);
                return 1;
            }
            // Accept lowercase with toupper()
            parity = toupper((unsigned char)p[0]);
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
        } else {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Unknown option: %s", argv[i]);
            WINDMI_LOG_INFO(LOG_TAG_MAIN, "Use --help for usage information");
            return 1;
        }
    }

    // Apply --log-file: add file output alongside console
    if (!log_file_path.empty()) {
        windmi::Logger::instance().addOutput(
            std::make_unique<windmi::FileLogOutput>(log_file_path));
    }

    // Acquire exclusive lock before doing anything
    if (force_lock) {
        // --force: remove stale lock file and try again
        // (flock is kernel-managed, but this allows overriding if needed)
        struct stat st;
        if (stat(LOCK_FILE, &st) == 0) {
            WINDMI_LOG_INFO(LOG_TAG_MAIN, "--force: removing stale lock file %s", LOCK_FILE);
            unlink(LOCK_FILE);
        }
    }
    if (acquire_lock() != 0) {
        return 1;
    }

    // Register atexit handler as safety net for lock release
    atexit(atexit_release_lock);

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE (client disconnects)

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Rotenso Windmi Controller");
    if (!demo_mode) {
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Modbus gateway: %s:%d", modbus_ip, modbus_port);
    }
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Web server: %s:%d", WEB_SERVER_IP, web_port);

    std::string resolved_static_dir = resolve_static_dir(static_dir);
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Static files: %s", resolved_static_dir.c_str());

    // Create SPSC queues
    windmi::CmdQueue cmd_queue;
    windmi::StatusQueue status_queue;

    // Create Modbus client (interface pointer for demo mode support)
    std::unique_ptr<windmi::IModbusClient> modbus_client;

    // Validate CLI options: mutual exclusion between TCP/IP and serial
    if (serial_specified) {
        // Serial mode: --serial implies no --ip or --port
        if (ip_specified || port_specified) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "--serial is mutually exclusive with --ip and --port");
            release_lock();
            return 1;
        }
        if (demo_mode) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "--serial is mutually exclusive with --demo");
            release_lock();
            return 1;
        }
    }
    
    // Validate serial-specific options require --serial
    if (serial_config_specified && !serial_specified) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN,
            "--baud, --parity, --stop-bits, and --rs485 require --serial to be specified");
        release_lock();
        return 1;
    }

    // Demo mode: use simulated client
    if (demo_mode) {
        modbus_client = std::make_unique<windmi::SimulatedModbusClient>();
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "DEMO MODE: using simulated Windmi device, no Modbus socket will be opened");
        // Use different default port for demo mode to avoid conflicts
        if (web_port == WEB_SERVER_PORT) {
            web_port = 10000;
        }
    } else if (!serial_device.empty()) {
        // Serial mode: use ModbusSerialClient
        modbus_client = std::make_unique<windmi::ModbusSerialClient>(
            serial_device, baud_rate, parity, stop_bits, rs485_enabled, MODBUS_SLAVE_ID);
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Serial Modbus RTU: %s @ %d %c%d", 
            serial_device.c_str(), baud_rate, parity, stop_bits);
        if (rs485_enabled) {
            WINDMI_LOG_INFO(LOG_TAG_MAIN, "RS-485 direction control enabled");
        }
    } else {
        // TCP mode: use ModbusClient
        modbus_client = std::make_unique<windmi::ModbusClient>(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
    }

    // Self-test mode: run self-test and exit (works with any transport)
    if (run_selftest) {
        if (!modbus_client->connect()) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to connect for self-test");
            release_lock();
            return 1;
        }

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

    // Drain/quiet period
    usleep(150000);  // 150ms
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Drain period complete");

    // Write OFF mode via dedicated shutdown client (skip in demo mode)
    if (!demo_mode) {
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Writing OFF mode via dedicated client...");
        bool off_written = false;
        
        try {
            std::unique_ptr<windmi::IModbusClient> shutdown_client;
            
            if (!serial_device.empty()) {
                // Serial mode: use ModbusSerialClient
                shutdown_client = std::make_unique<windmi::ModbusSerialClient>(
                    serial_device, baud_rate, parity, stop_bits, rs485_enabled, MODBUS_SLAVE_ID);
                WINDMI_LOG_INFO(LOG_TAG_MAIN, "Shutdown: using serial client on %s", serial_device.c_str());
            } else {
                // TCP mode: use ModbusClient
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
                    usleep(100000);  // 100ms retry delay
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