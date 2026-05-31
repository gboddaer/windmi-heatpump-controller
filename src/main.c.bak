#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <errno.h>
#include "config.h"
#include "modbus_client.h"
#include "control_loop.h"
#include "web_server.h"
#include "spsc_queue.h"
#include "selftest.h"

#define LOCK_FILE "/tmp/windmi-controller.lock"

static volatile sig_atomic_t g_running = 1;
static int g_lock_fd = -1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    // Wake up the web server poll loop
    web_server_stop();
}

// Acquire exclusive lock to prevent multiple instances
static int acquire_lock(void) {
    g_lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0644);
    if (g_lock_fd < 0) {
        fprintf(stderr, "[Main] Failed to open lock file %s: %s\n", LOCK_FILE, strerror(errno));
        return -1;
    }
    
    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK) {
            fprintf(stderr, "[Main] Another instance is already running (lock held)\n");
            close(g_lock_fd);
            g_lock_fd = -1;
            return -1;
        }
        fprintf(stderr, "[Main] Failed to acquire lock: %s\n", strerror(errno));
        close(g_lock_fd);
        g_lock_fd = -1;
        return -1;
    }
    
    // Write PID to lock file for debugging
    char pid_buf[32];
    int len = snprintf(pid_buf, sizeof(pid_buf), "%d\n", getpid());
    lseek(g_lock_fd, 0, SEEK_SET);
    write(g_lock_fd, pid_buf, len);
    ftruncate(g_lock_fd, len);
    
    printf("[Main] Lock acquired (PID: %d)\n", getpid());
    return 0;
}

static void release_lock(void) {
    if (g_lock_fd >= 0) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
        printf("[Main] Lock released\n");
    }
}

int main(int argc, char *argv[]) {
    char *modbus_ip = MODBUS_GATEWAY_IP;
    int modbus_port = MODBUS_GATEWAY_PORT;
    int web_port = WEB_SERVER_PORT;

    bool run_selftest = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --ip <address>  Modbus gateway IP (default: %s)\n", MODBUS_GATEWAY_IP);
            printf("  --port <port>   Modbus gateway port (default: %d)\n", MODBUS_GATEWAY_PORT);
            printf("  --web <port>    Web server HTTP port (default: %d)\n", WEB_SERVER_PORT);
            printf("  --selftest      Run self-test and exit\n");
            printf("  --help          Show this help message\n");
            return 0;
        } else if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
            modbus_ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            modbus_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--web") == 0 && i + 1 < argc) {
            web_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--selftest") == 0) {
            run_selftest = true;
        }
    }

    // Acquire exclusive lock before doing anything
    if (acquire_lock() != 0) {
        return 1;
    }
    
    // Set up signal handler to release lock on exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("[Main] Rotenso Windmi Controller\n");
    printf("[Main] Modbus gateway: %s:%d\n", modbus_ip, modbus_port);
    printf("[Main] Web server: %s:%d\n", WEB_SERVER_IP, web_port);

    spsc_cmd_t_queue_t cmd_queue;
    spsc_status_snapshot_t_queue_t status_queue;
    memset(&cmd_queue, 0, sizeof(cmd_queue));
    memset(&status_queue, 0, sizeof(status_queue));

    modbus_client_t *client = modbus_client_create(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
    if (!client) {
        fprintf(stderr, "[Main] Failed to create Modbus client\n");
        return 1;
    }

    if (run_selftest) {
        // Run self-test and exit
        selftest_report_t selftest_result = selftest_run(client);
        selftest_print_report(&selftest_result);
        printf("Self-test: %d/%d registers passed\n", selftest_result.passed, selftest_result.total);
        if (selftest_result.all_critical_passed) {
            printf("Self-test PASSED\n");
        } else {
            printf("Self-test FAILED\n");
        }
        modbus_client_destroy(client);
        return selftest_result.all_critical_passed ? 0 : 1;
    }
    
    if (control_loop_start(client, &cmd_queue, &status_queue, false) != 0) {
        fprintf(stderr, "[Main] Failed to start control loop\n");
        modbus_client_destroy(client);
        return 1;
    }

    if (web_server_init(web_port, "static", &cmd_queue, &status_queue) != 0) {
        fprintf(stderr, "[Main] Failed to initialize web server\n");
        control_loop_stop();
        modbus_client_destroy(client);
        return 1;
    }

    printf("[Main] Server started. Press Ctrl+C to stop.\n");

    web_server_run();

    printf("[Main] Shutting down...\n");
    
    // Signal control loop to stop
    control_loop_stop();
    printf("[Shutdown] Control loop stopped\n");
    
    // Wait for control loop thread to join (should be immediate after stop flag)
    usleep(150000); // 150ms drain/quiet period
    printf("[Shutdown] Drain period complete\n");
    
    // Set heat pump to OFF via dedicated client
    printf("[Shutdown] Writing OFF mode via dedicated client...\n");
    modbus_client_t *shutdown_client = modbus_client_create(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
    if (shutdown_client) {
        // Try to connect and write with retries
        bool off_written = false;
        for (int attempt = 1; attempt <= 3; attempt++) {
            if (modbus_client_connect(shutdown_client)) {
                // Flush stale data first
                modbus_client_flush_buffer(shutdown_client);
                
                // Set mode to OFF (0)
                uint16_t mode_off = 0;
                if (modbus_write_register(shutdown_client, REG_RUNNING_MODE, mode_off) == 0) {
                    printf("[Shutdown] OFF write OK (attempt %d)\n", attempt);
                    off_written = true;
                    modbus_client_disconnect(shutdown_client);
                    break;
                } else {
                    fprintf(stderr, "[Shutdown] OFF write failed (attempt %d)\n", attempt);
                    modbus_client_disconnect(shutdown_client);
                }
            } else {
                fprintf(stderr, "[Shutdown] Connect failed (attempt %d)\n", attempt);
            }
            
            // Retry delay
            if (attempt < 3) {
                usleep(100000); // 100ms before retry
            }
        }
        
        modbus_client_destroy(shutdown_client);
        shutdown_client = NULL;
        
        if (!off_written) {
            fprintf(stderr, "[Shutdown] OFF mode write failed after 3 attempts\n");
        }
    } else {
        fprintf(stderr, "[Shutdown] Failed to create shutdown client\n");
    }
    
    modbus_client_destroy(client);

    printf("[Main] Goodbye!\n");
    
    release_lock();
    return 0;
}
