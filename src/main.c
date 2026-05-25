#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "config.h"
#include "modbus_client.h"
#include "control_loop.h"
#include "web_server.h"
#include "spsc_queue.h"

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    // Wake up the web server poll loop
    web_server_stop();
}

int main(int argc, char *argv[]) {
    char *modbus_ip = MODBUS_GATEWAY_IP;
    int modbus_port = MODBUS_GATEWAY_PORT;
    int web_port = WEB_SERVER_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --ip <address>  Modbus gateway IP (default: %s)\n", MODBUS_GATEWAY_IP);
            printf("  --port <port>   Modbus gateway port (default: %d)\n", MODBUS_GATEWAY_PORT);
            printf("  --web <port>    Web server HTTP port (default: %d)\n", WEB_SERVER_PORT);
            printf("  --help          Show this help message\n");
            return 0;
        } else if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
            modbus_ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            modbus_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--web") == 0 && i + 1 < argc) {
            web_port = atoi(argv[++i]);
        }
    }

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

    if (control_loop_start(client, &cmd_queue, &status_queue) != 0) {
        fprintf(stderr, "[Main] Failed to start control loop (self-test failed)\n");
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
    control_loop_stop();
    modbus_client_destroy(client);

    printf("[Main] Goodbye!\n");
    return 0;
}
