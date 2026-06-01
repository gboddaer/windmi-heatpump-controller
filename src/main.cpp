/**
 * @file src/main.cpp
 * @brief Main entry point for Windmi Controller
 *
 * Matches master branch main.c functionality:
 * - Instance lock via flock()
 * - CLI: --ip, --port, --web, --selftest, --help
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
#include "modbus/SimulatedModbusClient.hpp"
#include "web/WebServer.hpp"
#include "crc16.h"

extern "C" {
#include "selftest.h"
}

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
    fprintf(stderr, "[Main] Warning: static directory \"%s\" not found\n", dir.c_str());
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
        fprintf(stderr, "[Main] Failed to open lock file %s: %s\n", LOCK_FILE, strerror(errno));
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
                fprintf(stderr, "[Main] Another instance is already running (PID %d)\n",
                        existing_pid);
            } else if (existing_pid > 0) {
                fprintf(stderr, "[Main] Lock held by stale process %d (not running)\n",
                        existing_pid);
                fprintf(stderr, "[Main] Remove %s or use --force to override\n", LOCK_FILE);
            } else {
                fprintf(stderr, "[Main] Another instance is already running\n");
            }
        } else {
            fprintf(stderr, "[Main] Failed to acquire lock: %s\n", strerror(errno));
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

    printf("[Main] Lock acquired (PID: %d)\n", getpid());
    return 0;
}

static void release_lock() {
    if (g_lock_fd >= 0) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
        printf("[Main] Lock released\n");
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
    int web_port = WEB_SERVER_PORT;
    std::string static_dir = "static";
    bool run_selftest = false;
    bool demo_mode = false;
    bool force_lock = false;

    // Parse CLI arguments (long and short forms)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -i, --ip <address>  Modbus gateway IP (default: %s)\n", MODBUS_GATEWAY_IP);
            printf("  -p, --port <port>   Modbus gateway port (default: %d)\n", MODBUS_GATEWAY_PORT);
            printf("  -w, --web <port>    Web server HTTP port (default: %d)\n", WEB_SERVER_PORT);
            printf("  -t, --static-dir <dir>  Static files directory (default: static)\n");
            printf("  -f, --force         Force start even if lock is held by stale process\n");
            printf("  -s, --selftest      Run self-test and exit\n");
            printf("  -d, --demo          Run in demo mode with simulated Windmi device\n");
            printf("  -h, --help          Show this help message\n");
            return 0;
        } else if ((strcmp(argv[i], "--ip") == 0 || strcmp(argv[i], "-i") == 0) && i + 1 < argc) {
            modbus_ip = argv[++i];
        } else if ((strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0) && i + 1 < argc) {
            modbus_port = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--web") == 0 || strcmp(argv[i], "-w") == 0) && i + 1 < argc) {
            web_port = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--static-dir") == 0 || strcmp(argv[i], "-t") == 0) && i + 1 < argc) {
            static_dir = argv[++i];
        } else if (strcmp(argv[i], "--selftest") == 0 || strcmp(argv[i], "-s") == 0) {
            run_selftest = true;
        } else if (strcmp(argv[i], "--demo") == 0 || strcmp(argv[i], "-d") == 0) {
            demo_mode = true;
        } else if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
            force_lock = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Use --help for usage information.\n");
            return 1;
        }
    }

    // Acquire exclusive lock before doing anything
    if (force_lock) {
        // --force: remove stale lock file and try again
        // (flock is kernel-managed, but this allows overriding if needed)
        struct stat st;
        if (stat(LOCK_FILE, &st) == 0) {
            fprintf(stderr, "[Main] --force: removing stale lock file %s\n", LOCK_FILE);
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

    printf("[Main] Rotenso Windmi Controller\n");
    if (!demo_mode) {
        printf("[Main] Modbus gateway: %s:%d\n", modbus_ip, modbus_port);
    }
    printf("[Main] Web server: %s:%d\n", WEB_SERVER_IP, web_port);

    std::string resolved_static_dir = resolve_static_dir(static_dir);
    printf("[Main] Static files: %s\n", resolved_static_dir.c_str());

    // Create SPSC queues
    windmi::CmdQueue cmd_queue;
    windmi::StatusQueue status_queue;

    // Create Modbus client (interface pointer for demo mode support)
    std::unique_ptr<windmi::IModbusClient> modbus_client;

    // Demo mode: reject --selftest, use simulated client
    if (demo_mode) {
        if (run_selftest) {
            fprintf(stderr, "[Main] --selftest is not supported in demo mode\n");
            release_lock();
            return 1;
        }
        modbus_client = std::make_unique<windmi::SimulatedModbusClient>();
        printf("[Main] DEMO MODE: using simulated Windmi device, no Modbus socket will be opened\n");
    } else {
        modbus_client = std::make_unique<windmi::ModbusClient>(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
    }

    // Self-test mode: run self-test and exit (only for non-demo)
    if (run_selftest) {
        // Cast is safe here: we only reach here in non-demo mode
        auto* real_client = dynamic_cast<windmi::ModbusClient*>(modbus_client.get());
        if (!real_client || !real_client->connect()) {
            fprintf(stderr, "[Main] Failed to connect to Modbus for self-test\n");
            release_lock();
            return 1;
        }

        // Get C client for selftest_run (which takes modbus_client_t*)
        modbus_client_t* c_client = static_cast<modbus_client_t*>(real_client->getCClient());
        selftest_report_t selftest_result = selftest_run(c_client);
        selftest_print_report(&selftest_result);
        printf("Self-test: %d/%d registers passed\n", selftest_result.passed, selftest_result.total);
        if (selftest_result.all_critical_passed) {
            printf("Self-test PASSED\n");
        } else {
            printf("Self-test FAILED\n");
        }
        real_client->disconnect();
        release_lock();
        return selftest_result.all_critical_passed ? 0 : 1;
    }

    // Connect to Modbus gateway (no-op for simulated client)
    if (!modbus_client->connect()) {
        fprintf(stderr, "[Main] Failed to connect to Modbus gateway\n");
        release_lock();
        return 1;
    }

    // Create and start control loop (pass Modbus client + queues)
    g_control_loop = new windmi::ControlLoop();
    if (!g_control_loop->start(modbus_client.get(), &cmd_queue, &status_queue)) {
        fprintf(stderr, "[Main] Failed to start control loop\n");
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
        fprintf(stderr, "[Main] Failed to initialize web server: %s\n", e.what());
        g_control_loop->stop();
        delete g_control_loop;
        g_control_loop = nullptr;
        release_lock();
        return 1;
    }

    printf("[Main] Server started. Press Ctrl+C to stop.\n");

    // Poll web server until a signal requests shutdown. The signal handler only
    // flips g_running; all C++ calls happen here outside signal context.
    while (g_running) {
        g_web_server->poll(100);
    }
    g_web_server->stop();

    printf("[Main] Shutting down...\n");

    // Stop control loop
    g_control_loop->stop();
    printf("[Shutdown] Control loop stopped\n");

    // Drain/quiet period
    usleep(150000);  // 150ms
    printf("[Shutdown] Drain period complete\n");

    // Write OFF mode via dedicated shutdown client (skip in demo mode)
    if (!demo_mode) {
        printf("[Shutdown] Writing OFF mode via dedicated client...\n");
        bool off_written = false;
        try {
            windmi::ModbusClient shutdown_client(modbus_ip, modbus_port, MODBUS_SLAVE_ID);

            for (int attempt = 1; attempt <= 3; attempt++) {
                if (shutdown_client.connect()) {
                    shutdown_client.flushBuffer();

                    try {
                        shutdown_client.writeRegister(REG_RUNNING_MODE, 0);
                        printf("[Shutdown] OFF write OK (attempt %d)\n", attempt);
                        off_written = true;
                        shutdown_client.disconnect();
                        break;
                    } catch (const windmi::ModbusException&) {
                        fprintf(stderr, "[Shutdown] OFF write failed (attempt %d)\n", attempt);
                        shutdown_client.disconnect();
                    }
                } else {
                    fprintf(stderr, "[Shutdown] Connect failed (attempt %d)\n", attempt);
                }

                if (attempt < 3) {
                    usleep(100000);  // 100ms retry delay
                }
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[Shutdown] Exception creating shutdown client: %s\n", e.what());
        }

        if (!off_written) {
            fprintf(stderr, "[Shutdown] OFF mode write failed after 3 attempts\n");
        }
    } else {
        printf("[Shutdown] DEMO MODE: skipping real OFF write\n");
    }

    // Cleanup
    delete g_web_server;
    g_web_server = nullptr;

    delete g_control_loop;
    g_control_loop = nullptr;

    printf("[Main] Goodbye!\n");

    release_lock();
    return 0;
}