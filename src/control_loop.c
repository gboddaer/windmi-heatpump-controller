#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "control_loop.h"
#include "config.h"
#include "selftest.h"

typedef enum {
    PRIORITY_DHW,
    PRIORITY_HEATING
} priority_mode_t;

// Global state for the control loop thread
static pthread_t control_loop_thread;
static volatile bool control_loop_running = false;
static volatile bool stop_requested = false;

// Thread-local state
static modbus_client_t *thread_client = NULL;
static spsc_cmd_t_queue_t *thread_cmd_queue = NULL;
static spsc_status_snapshot_t_queue_t *thread_status_queue = NULL;

// Current control state
static priority_mode_t current_priority = PRIORITY_HEATING;
static float last_heating_target = 45.0f;

// Convert raw register value to temperature (divide by 10)
static inline float raw_to_temp(int16_t raw) {
    return raw / 10.0f;
}

// Convert temperature to raw register value (multiply by 10)
static inline int16_t temp_to_raw(float temp) {
    return (int16_t)(temp * 10.0f);
}

// Check if DHW needs heating (tank temp > hysteresis below target)
static bool dhw_needs_heating(float tank_temp, float target, float hysteresis) {
    return (target - tank_temp) > hysteresis;
}

// Check if space heating is needed based on leaving water temperature
// vs heating target with hysteresis
static bool heating_is_needed(status_snapshot_t *status) {
    if (!status) return false;
    return (status->heating_target - status->leaving_water_temp) > HEATING_HYSTERESIS_C;
}

// Determine the desired priority mode based on current conditions
static priority_mode_t determine_priority(status_snapshot_t *status) {
    if (!status) return PRIORITY_HEATING;
    
    // If DHW tank is more than 3°C below target, prioritize DHW
    if (dhw_needs_heating(status->dhw_tank_temp, status->dhw_target, DHW_HYSTERESIS_C)) {
        return PRIORITY_DHW;
    }
    
    // If DHW is satisfied and space heating is needed, prioritize heating
    if (heating_is_needed(status)) {
        return PRIORITY_HEATING;
    }
    
    return PRIORITY_HEATING;
}

// Set the running mode on the heat pump
static int set_running_mode(int mode) {
    if (!thread_client) {
        return -1;
    }
    
    if (modbus_write_register(thread_client, REG_RUNNING_MODE, (uint16_t)mode) != 0) {
        fprintf(stderr, "Failed to set running mode to %d\n", mode);
        return -1;
    }
    
    printf("Control loop: Set running mode to %d\n", mode);
    return 0;
}

// Set DHW target temperature
static int set_dhw_target(float temp) {
    if (!thread_client) {
        return -1;
    }
    
    int16_t raw_temp = temp_to_raw(temp);
    if (modbus_write_register(thread_client, REG_DHW_TARGET, (uint16_t)raw_temp) != 0) {
        fprintf(stderr, "Failed to set DHW target to %.1f\n", temp);
        return -1;
    }
    
    printf("Control loop: Set DHW target to %.1f C\n", temp);
    return 0;
}

// Set heating target temperature
static int set_heating_target(float temp) {
    if (!thread_client) {
        return -1;
    }
    
    int16_t raw_temp = temp_to_raw(temp);
    if (modbus_write_register(thread_client, REG_HEATING_TARGET, (uint16_t)raw_temp) != 0) {
        fprintf(stderr, "Failed to set heating target to %.1f\n", temp);
        return -1;
    }
    
    printf("Control loop: Set heating target to %.1f C\n", temp);
    return 0;
}

// Read registers and update status snapshot
// Returns true if all critical registers were read successfully.
// On partial failure, status fields retain their last-known values.
static bool read_status(status_snapshot_t *status) {
    if (!thread_client || !status) {
        return false;
    }
    
    int16_t raw;
    bool ok = true;

    // Read outdoor temp (0x0001) — non-critical for control
    if (modbus_read_register(thread_client, REG_OUTDOOR_TEMP, &raw) == 0) {
        status->outdoor_temp = raw_to_temp(raw);
    } else {
        ok = false;
    }

    // Read indoor temp (0x0002) — optional
    if (modbus_read_register(thread_client, REG_INDOOR_TEMP, &raw) == 0) {
        status->indoor_temp = raw_to_temp(raw);
    }

    // Read leaving water temp (0x0004) — critical for heating logic
    if (modbus_read_register(thread_client, REG_LEAVING_WATER_TEMP, &raw) == 0) {
        status->leaving_water_temp = raw_to_temp(raw);
    } else {
        ok = false;
    }
    
    // Read DHW tank temp (0x1C5B) — critical for DHW priority logic
    // If read fails, status->dhw_tank_temp retains its value (0.0 initially,
    // last-known value on subsequent calls). We do NOT leave it at 0.0
    // because that would incorrectly trigger DHW priority.
    if (modbus_read_register(thread_client, REG_DHW_TANK_TEMP, &raw) == 0) {
        status->dhw_tank_temp = raw_to_temp(raw);
    } else {
        fprintf(stderr, "Failed to read DHW tank temp\n");
        ok = false;
    }
    
    // Read running mode (0x002D) — critical
    if (modbus_read_register(thread_client, REG_RUNNING_MODE, &raw) == 0) {
        status->running_mode = raw;
        status->is_running = (raw != MODE_OFF);
    } else {
        ok = false;
    }
    
    // Read DHW target (0x0194) — critical
    if (modbus_read_register(thread_client, REG_DHW_TARGET, &raw) == 0) {
        status->dhw_target = raw_to_temp(raw);
    } else {
        ok = false;
    }
    
    // Read heating target (0x0191) — critical
    if (modbus_read_register(thread_client, REG_HEATING_TARGET, &raw) == 0) {
        status->heating_target = raw_to_temp(raw);
        last_heating_target = status->heating_target;
    } else {
        status->heating_target = last_heating_target;
    }

    // Read DHW priority (0x028F) — critical
    if (modbus_read_register(thread_client, REG_DHW_PRIORITY, &raw) == 0) {
        status->dhw_priority = (raw != 0);
        current_priority = status->dhw_priority ? PRIORITY_DHW : PRIORITY_HEATING;
    } else {
        ok = false;
    }
    
    status->device_online = ok;
    return ok;
}

// Process incoming commands from the queue
static void process_commands(void) {
    if (!thread_cmd_queue) {
        return;
    }
    
    cmd_t cmd;
    while (spsc_pop_cmd_t(thread_cmd_queue, &cmd)) {
        switch (cmd.type) {
            case CMD_SET_DHW_TEMP:
                set_dhw_target(cmd.float_val);
                break;
                
            case CMD_SET_HEATING_TEMP:
                set_heating_target(cmd.float_val);
                last_heating_target = cmd.float_val;
                break;
                
            case CMD_SET_PRIORITY:
                // int_val == 1 means DHW priority, int_val == 0 means heating priority
                // (matches REG_DHW_PRIORITY register: 1=DHW, 0=heating)
                if (modbus_write_register(thread_client, REG_DHW_PRIORITY, (uint16_t)cmd.int_val) == 0) {
                    current_priority = (cmd.int_val == 1) ? PRIORITY_DHW : PRIORITY_HEATING;
                    printf("Control loop: Priority set to %s\n",
                           cmd.int_val == 1 ? "DHW" : "Heating");
                } else {
                    fprintf(stderr, "Control loop: Failed to set priority register\n");
                }
                break;
                
            default:
                fprintf(stderr, "Unknown command type: %d\n", cmd.type);
                break;
        }
    }
}

// Apply control logic based on current state
// When DHW priority is active:
//   - If tank < target - 3°C, switch heat pump to DHW mode
//   - If tank >= target and space heating needed, switch back to Heat mode
static void apply_control_logic(status_snapshot_t *status) {
    if (!status) {
        return;
    }
    
    priority_mode_t desired = determine_priority(status);
    
    if (desired == PRIORITY_DHW) {
        // DHW tank needs heating: switch to DHW mode if currently heating
        if (status->running_mode == MODE_HEAT) {
            printf("Control loop: DHW needs heating (tank=%.1f target=%.1f), switching to DHW mode\n",
                   status->dhw_tank_temp, status->dhw_target);
            if (set_running_mode(MODE_DHW) == 0) {
                current_priority = PRIORITY_DHW;
            }
        }
        if (current_priority != PRIORITY_DHW) {
            printf("Control loop: Switching to DHW priority\n");
            current_priority = PRIORITY_DHW;
        }
    } else {
        // DHW satisfied: switch back to heating if needed
        if (status->running_mode == MODE_DHW && heating_is_needed(status)) {
            printf("Control loop: DHW target reached (tank=%.1f), switching to Heat mode\n",
                   status->dhw_tank_temp);
            if (set_running_mode(MODE_HEAT) == 0) {
                current_priority = PRIORITY_HEATING;
            }
        }
        if (current_priority != PRIORITY_HEATING) {
            printf("Control loop: Switching to heating priority\n");
            current_priority = PRIORITY_HEATING;
        }
    }
}

// Main control loop thread function
static void *control_loop_thread_func(void *arg) {
    (void)arg;
    
    printf("Control loop thread started\n");
    
    // Main loop
    while (!stop_requested) {
        struct timespec start_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        // Check connection and reconnect if needed
        if (!thread_client || !modbus_client_is_connected(thread_client)) {
            printf("Control loop: Not connected, attempting to reconnect...\n");
            
            int retries = 0;
            while (!stop_requested && retries < MODBUS_MAX_RETRIES) {
                if (modbus_client_connect(thread_client)) {
                    printf("Control loop: Reconnected successfully\n");
                    break;
                }
                retries++;
                sleep(MODBUS_RECONNECT_INTERVAL_S);
            }
            
            if (retries >= MODBUS_MAX_RETRIES) {
                fprintf(stderr, "Control loop: Failed to reconnect after %d attempts\n", retries);
                sleep(MODBUS_RECONNECT_INTERVAL_S);
                continue;
            }
        }
        
        // Process any pending commands
        process_commands();
        
        // Read current status
        status_snapshot_t status;
        memset(&status, 0, sizeof(status));
        
        if (read_status(&status)) {
            // Apply control logic
            apply_control_logic(&status);
            
            // Publish status to queue
            if (!spsc_push_status_snapshot_t(thread_status_queue, status)) {
                fprintf(stderr, "Control loop: Status queue full, dropping snapshot\n");
            }
        } else {
            fprintf(stderr, "Control loop: Failed to read status\n");
            // Publish offline status
            status.device_online = false;
            if (!spsc_push_status_snapshot_t(thread_status_queue, status)) {
                fprintf(stderr, "Control loop: Status queue full, dropping snapshot\n");
            }
        }
        
        // Calculate sleep time to maintain interval
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        long elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 +
                          (end_time.tv_nsec - start_time.tv_nsec) / 1000000;
        long sleep_ms = CONTROL_LOOP_INTERVAL_S * 1000 - elapsed_ms;
        
        if (sleep_ms > 0) {
            usleep(sleep_ms * 1000);
        }
    }
    
    // Cleanup
    if (thread_client) {
        modbus_client_disconnect(thread_client);
    }
    
    printf("Control loop thread stopped\n");
    control_loop_running = false;
    
    return NULL;
}

int control_loop_start(modbus_client_t *client,
                       spsc_cmd_t_queue_t *cmd_queue,
                       spsc_status_snapshot_t_queue_t *status_queue) {
    if (control_loop_running) {
        printf("Control loop already running\n");
        return -1;
    }
    
    // Initialize thread-local state
    thread_client = client;
    thread_cmd_queue = cmd_queue;
    thread_status_queue = status_queue;
    stop_requested = false;
    
    // Connect to Modbus gateway before starting thread
    if (!modbus_client_connect(client)) {
        fprintf(stderr, "Failed to connect to Modbus gateway\n");
        return -1;
    }
    
    // Run self-test synchronously before starting the thread
    selftest_report_t selftest_result = selftest_run(client);
    printf("Self-test: %d/%d registers passed\n", selftest_result.passed, selftest_result.total);
    
    if (!selftest_result.all_critical_passed) {
        fprintf(stderr, "Self-test failed: critical registers did not pass\n");
        selftest_print_report(&selftest_result);
        modbus_client_disconnect(client);
        return -1;
    }
    
    // Create the thread
    int ret = pthread_create(&control_loop_thread, NULL, control_loop_thread_func, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to create control loop thread: %s\n", strerror(ret));
        modbus_client_disconnect(client);
        return -1;
    }
    
    control_loop_running = true;
    printf("Control loop started successfully\n");
    return 0;
}

void control_loop_stop(void) {
    if (!control_loop_running) {
        printf("Control loop not running\n");
        return;
    }
    
    printf("Stopping control loop...\n");
    stop_requested = true;
    
    // Wait for thread to finish (with timeout)
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5; // 5 second timeout
    
    int ret = pthread_timedjoin_np(control_loop_thread, NULL, &timeout);
    if (ret == 0) {
        printf("Control loop stopped successfully\n");
    } else {
        fprintf(stderr, "Timeout waiting for control loop to stop\n");
        pthread_cancel(control_loop_thread);
    }
    
    control_loop_running = false;
    thread_client = NULL;
    thread_cmd_queue = NULL;
    thread_status_queue = NULL;
}

bool control_loop_is_running(void) {
    return control_loop_running;
}
