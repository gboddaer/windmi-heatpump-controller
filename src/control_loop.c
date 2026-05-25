#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
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
static float last_dhw_target = 55.0f;
static float last_heating_target = 45.0f;
static int16_t last_dhw_tank_temp_raw = 0;

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

// Check if space heating is needed
// For now, we assume heating is always needed if in heat mode
// This could be expanded with thermostat logic later
static bool heating_is_needed(int running_mode) {
    return running_mode == MODE_HEAT;
}

// Determine the desired priority mode based on current conditions
static priority_mode_t determine_priority(float tank_temp, float dhw_target,
                                          float heating_target __attribute__((unused)),
                                          int running_mode) {
    // If DHW needs heating, prioritize DHW
    if (dhw_needs_heating(tank_temp, dhw_target, DHW_HYSTERESIS_C)) {
        return PRIORITY_DHW;
    }
    
    // If DHW is satisfied and space heating is needed, prioritize heating
    if (heating_is_needed(running_mode)) {
        return PRIORITY_HEATING;
    }
    
    // Default to heating mode if neither needs attention
    return PRIORITY_HEATING;
}

// Set the running mode on the heat pump
__attribute__((unused))
static int set_running_mode(int mode) {
    if (!thread_client || !thread_cmd_queue) {
        return -1;
    }
    
    // Write mode to register 0x002D
    int16_t raw_mode = (int16_t)mode;
    if (modbus_write_register(thread_client, REG_RUNNING_MODE, (uint16_t)raw_mode) != 0) {
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
static bool read_status(status_snapshot_t *status) {
    if (!thread_client || !status) {
        return false;
    }
    
    int16_t values[6];
    
    // Read outdoor temp (0x0001)
    if (modbus_read_register(thread_client, REG_OUTDOOR_TEMP, &values[0]) != 0) {
        fprintf(stderr, "Failed to read outdoor temp\n");
        return false;
    }
    
    // Read indoor temp (0x0002)
    if (modbus_read_register(thread_client, REG_INDOOR_TEMP, &values[1]) != 0) {
        fprintf(stderr, "Failed to read indoor temp\n");
        return false;
    }
    
    // Read entering water temp (0x0003)
    if (modbus_read_register(thread_client, REG_ENTERING_WATER_TEMP, &values[2]) != 0) {
        fprintf(stderr, "Failed to read entering water temp\n");
        return false;
    }
    
    // Read leaving water temp (0x0004)
    if (modbus_read_register(thread_client, REG_LEAVING_WATER_TEMP, &values[3]) != 0) {
        fprintf(stderr, "Failed to read leaving water temp\n");
        return false;
    }
    
    // Read running mode (0x002D)
    if (modbus_read_register(thread_client, REG_RUNNING_MODE, &values[4]) != 0) {
        fprintf(stderr, "Failed to read running mode\n");
        return false;
    }
    
    // Read DHW target (0x0194)
    if (modbus_read_register(thread_client, REG_DHW_TARGET, &values[5]) != 0) {
        fprintf(stderr, "Failed to read DHW target\n");
        return false;
    }
    
    status->outdoor_temp = raw_to_temp(values[0]);
    status->indoor_temp = raw_to_temp(values[1]);
    status->leaving_water_temp = raw_to_temp(values[3]);
    status->running_mode = values[4];
    status->dhw_target = raw_to_temp(values[5]);
    status->device_online = true;
    status->is_running = true;
    
    return true;
}

// Read DHW tank temperature (separate register at 0x1C5B)
static bool read_dhw_tank_temp(float *temp) {
    if (!thread_client || !temp) {
        return false;
    }
    
    int16_t raw_temp;
    if (modbus_read_register(thread_client, REG_DHW_TANK_TEMP, &raw_temp) != 0) {
        fprintf(stderr, "Failed to read DHW tank temp\n");
        return false;
    }
    
    *temp = raw_to_temp(raw_temp);
    return true;
}

// Read heating target temperature
static bool read_heating_target(float *target) {
    if (!thread_client || !target) {
        return false;
    }
    
    int16_t raw_temp;
    if (modbus_read_register(thread_client, REG_HEATING_TARGET, &raw_temp) != 0) {
        fprintf(stderr, "Failed to read heating target\n");
        return false;
    }
    
    *target = raw_to_temp(raw_temp);
    return true;
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
                last_dhw_target = cmd.float_val;
                break;
                
            case CMD_SET_HEATING_TEMP:
                set_heating_target(cmd.float_val);
                last_heating_target = cmd.float_val;
                break;
                
            case CMD_SET_PRIORITY:
                current_priority = (cmd.int_val == 0) ? PRIORITY_DHW : PRIORITY_HEATING;
                printf("Control loop: Priority set to %s\n", 
                       cmd.int_val == 0 ? "DHW" : "Heating");
                break;
                
            default:
                fprintf(stderr, "Unknown command type: %d\n", cmd.type);
                break;
        }
    }
}

// Apply control logic based on current state
static void apply_control_logic(status_snapshot_t *status) {
    if (!status) {
        return;
    }
    
    float dhw_target = status->dhw_target;
    float heating_target = last_heating_target;
    int running_mode = status->running_mode;
    
    // Determine desired priority based on conditions
    priority_mode_t desired_priority = determine_priority(
        status->dhw_tank_temp,
        dhw_target,
        heating_target,
        running_mode
    );
    
    // Apply priority mode
    if (desired_priority == PRIORITY_DHW) {
        if (current_priority != PRIORITY_DHW) {
            printf("Control loop: Switching to DHW priority mode\n");
            current_priority = PRIORITY_DHW;
            // In DHW priority mode, the heat pump focuses on heating domestic hot water
            // The device handles this internally once we set the target
        }
    } else {
        if (current_priority != PRIORITY_HEATING) {
            printf("Control loop: Switching to heating priority mode\n");
            current_priority = PRIORITY_HEATING;
        }
    }
}

// Main control loop thread function
static void *control_loop_thread_func(void *arg) {
    (void)arg;
    
    printf("Control loop thread started\n");
    
    // Perform self-test
    selftest_report_t selftest_result = selftest_run(thread_client);
    printf("Self-test: %d/%d registers passed\n", selftest_result.passed, selftest_result.total);
    
    // Main loop
    while (!stop_requested) {
        struct timespec start_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        // Check connection
        if (!thread_client || !modbus_client_is_connected(thread_client)) {
            printf("Control loop: Not connected, attempting to reconnect...\n");
            
            // Try to reconnect
            int retries = 0;
            while (!stop_requested && retries < MODBUS_MAX_RETRIES) {
                if (modbus_client_connect(thread_client)) {
                    printf("Control loop: Reconnected successfully\n");
                    break;
                }
                retries++;
                printf("Control loop: Reconnection attempt %d failed, retrying...\n", retries);
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
        status.device_online = false;
        status.is_running = false;
        
        if (read_status(&status)) {
            // Read DHW tank temperature separately
            if (read_dhw_tank_temp(&status.dhw_tank_temp)) {
                last_dhw_tank_temp_raw = temp_to_raw(status.dhw_tank_temp);
            }
            
            // Read heating target
            if (!read_heating_target(&status.heating_target)) {
                status.heating_target = last_heating_target;
            }
            
            // Apply control logic
            apply_control_logic(&status);
            
            // Publish status to queue
            if (!spsc_push_status_snapshot_t(thread_status_queue, status)) {
                fprintf(stderr, "Control loop: Status queue full, dropping snapshot\n");
            }
        } else {
            fprintf(stderr, "Control loop: Failed to read status\n");
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

void control_loop_start(modbus_client_t *client,
                        spsc_cmd_t_queue_t *cmd_queue,
                        spsc_status_snapshot_t_queue_t *status_queue) {
    if (control_loop_running) {
        printf("Control loop already running\n");
        return;
    }
    
    // Initialize thread-local state
    thread_client = client;
    thread_cmd_queue = cmd_queue;
    thread_status_queue = status_queue;
    stop_requested = false;
    
    // Create the thread
    int ret = pthread_create(&control_loop_thread, NULL, control_loop_thread_func, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to create control loop thread: %s\n", strerror(ret));
        return;
    }
    
    control_loop_running = true;
    printf("Control loop started successfully\n");
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
