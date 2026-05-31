#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>
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
static priority_mode_t current_priority = PRIORITY_DHW;  // Default to DHW priority
static int current_mode = MODE_SET_HEAT_DHW;  // Device mode we last set (0, 1, or 2)
static float last_heating_target = 45.0f;

// Track desired working mode from user commands:
// -1 = not set (use automatic control logic)
//  0 = user wants OFF
//  1 = user wants DHW-only (set heating target to min)
//  2 = user wants Heating-only (set DHW target to min)
//  3 = user wants DHW+Heating
static int desired_working_mode = 3;  // Default to DHW+Heating

// Saved user target temperatures, so we can restore when switching modes.
// These are updated when the user explicitly sets a target.
static float saved_dhw_target = 46.0f;
static float saved_heating_target = 45.0f;

// Minimum targets used to effectively disable a circuit:
#define DHW_TARGET_MIN      40.0f   // Minimum DHW target (device limit)
#define HEATING_TARGET_MIN  25.0f   // Minimum heating target (device limit)

// Semaphore for immediate control loop wake-up
static pthread_mutex_t kick_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t kick_cond = PTHREAD_COND_INITIALIZER;
static bool kicked = false;

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
// Only modes 0 (Off), 1 (Cool+DHW), 2 (Heat+DHW) are valid device modes
static int set_running_mode(int mode) {
    if (!thread_client) {
        return -1;
    }
    
    // Validate mode: only 0, 1, 2 are allowed by the device
    if (mode < 0 || mode > 2) {
        fprintf(stderr, "Invalid device mode %d: only 0 (Off), 1 (Cool+DHW), 2 (Heat+DHW) supported\n", mode);
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
    
    // Read running mode setting (0x002C) — read/write, values: 0=Off, 1=Cool+DHW, 2=Heat+DHW
    // Also update our tracking of the current device mode
    if (modbus_read_register(thread_client, REG_RUNNING_MODE, &raw) == 0) {
        status->running_mode = raw;
        // Update current_mode from the device (may have been changed externally)
        if (raw == 0 || raw == 1 || raw == 2) {
            current_mode = raw;
        }
    } else {
        ok = false;
    }
    
    // Read running status (0x002D) — read-only, actual device state
    // Values: 0=Off, 1=Cool, 2=Heat, 4=DHW, 7=Defrost, 20=Anti-freeze
    if (modbus_read_register(thread_client, REG_RUNNING_STATUS, &raw) == 0) {
        status->running_status = raw;
        status->is_running = (raw != MODE_STATUS_OFF);
    } else {
        ok = false;
    }
    
    // Read DHW target (0x0194) — critical
    if (modbus_read_register(thread_client, REG_DHW_TARGET, &raw) == 0) {
        status->dhw_target = raw_to_temp(raw);
        // Only save user targets when in DHW+Heating mode (not overriding)
        if (desired_working_mode == 3) {
            saved_dhw_target = status->dhw_target;
        }
    } else {
        ok = false;
    }
    
    // Read heating target (0x0191) — critical
    if (modbus_read_register(thread_client, REG_HEATING_TARGET, &raw) == 0) {
        status->heating_target = raw_to_temp(raw);
        last_heating_target = status->heating_target;
        // Only save user targets when in DHW+Heating mode (not overriding)
        if (desired_working_mode == 3) {
            saved_heating_target = status->heating_target;
        }
    } else {
        status->heating_target = last_heating_target;
    }

    // Read DHW priority (0x02BF) — critical
    if (modbus_read_register(thread_client, REG_DHW_PRIORITY, &raw) == 0) {
        status->dhw_priority = (raw != 0);
        current_priority = status->dhw_priority ? PRIORITY_DHW : PRIORITY_HEATING;
        // Note: current_mode is managed by apply_control_logic, not here
    } else {
        ok = false;
    }
    
    // Read power monitoring registers
    int16_t ac_current_raw, dc_current_raw, ac_voltage_raw, dc_voltage_raw;
    if (modbus_read_register(thread_client, REG_AC_CURRENT, &ac_current_raw) == 0 &&
        modbus_read_register(thread_client, REG_DC_CURRENT, &dc_current_raw) == 0 &&
        modbus_read_register(thread_client, REG_AC_VOLTAGE, &ac_voltage_raw) == 0 &&
        modbus_read_register(thread_client, REG_DC_VOLTAGE, &dc_voltage_raw) == 0) {
        // Apply scaling factors per device spec
        status->ac_current = ac_current_raw * 2.0f;      // Actual = Display * 2
        status->dc_current = dc_current_raw * 4.0f;      // Actual = Display * 4
        status->ac_voltage = (float)ac_voltage_raw;      // Actual = Display
        status->dc_voltage = dc_voltage_raw / 2.0f;      // Actual = Display / 2
        status->ac_power = status->ac_voltage * status->ac_current;  // Power in Watts (AC)
    } else {
        status->ac_current = 0.0f;
        status->dc_current = 0.0f;
        status->ac_voltage = 0.0f;
        status->dc_voltage = 0.0f;
        status->ac_power = 0.0f;
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
    int cmd_count = 0;
    while (spsc_pop_cmd_t(thread_cmd_queue, &cmd)) {
        cmd_count++;
        printf("Control loop: Processing command #%d - type=%d", cmd_count, cmd.type);
        
        // Kick the loop to process immediately after handling commands
        
        switch (cmd.type) {
            case CMD_SET_DHW_TEMP:
                printf(" (CMD_SET_DHW_TEMP, temp=%.1f°C)\n", cmd.float_val);
                set_dhw_target(cmd.float_val);
                saved_dhw_target = cmd.float_val;  // Save user's desired target
                break;
                
            case CMD_SET_HEATING_TEMP:
                printf(" (CMD_SET_HEATING_TEMP, temp=%.1f°C)\n", cmd.float_val);
                set_heating_target(cmd.float_val);
                last_heating_target = cmd.float_val;
                saved_heating_target = cmd.float_val;  // Save user's desired target
                break;
                
            case CMD_SET_PRIORITY:
                printf(" (CMD_SET_PRIORITY, pri_val=%d)\n", cmd.int_val);
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
                
            case CMD_SET_RUNNING_MODE:
                printf(" (CMD_SET_RUNNING_MODE, mode=%d)\n", cmd.int_val);
                // Working mode: 0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating
                //
                // Device only supports: 0=Off, 1=Cool+DHW, 2=Heat+DHW
                // Strategy for single-purpose modes:
                // - DHW-only (1): Set device to Heat+DHW, but lower heating target to min
                //   so the compressor only runs to heat DHW.
                // - Heating-only (2): Set device to Heat+DHW, but lower DHW target to min
                //   so the compressor only runs for space heating.
                // - DHW+Heating (3): Set device to Heat+DHW, restore user's saved targets.
                // - Off (0): Set device to Off.
                
                desired_working_mode = cmd.int_val;
                
                int target_device_mode;
                switch (cmd.int_val) {
                    case 0:  target_device_mode = MODE_SET_OFF;        break;
                    case 1:  target_device_mode = MODE_SET_HEAT_DHW;   break;
                    case 2:  target_device_mode = MODE_SET_HEAT_DHW;   break;
                    case 3:  target_device_mode = MODE_SET_HEAT_DHW;   break;
                    default: target_device_mode = MODE_SET_HEAT_DHW;  break;
                }
                
                if (set_running_mode(target_device_mode) == 0) {
                    current_mode = target_device_mode;
                    printf("Control loop: Working mode set to %d (device mode=%d)\n", cmd.int_val, target_device_mode);
                } else {
                    fprintf(stderr, "Control loop: Failed to set running mode\n");
                    break;
                }
                
                // Override target temperatures and priority based on working mode
                switch (cmd.int_val) {
                    case 1:  // DHW-only: drop heating target to minimum, set DHW priority
                        printf("Control loop: DHW-only mode, setting heating target to min (%.1f)\n", HEATING_TARGET_MIN);
                        set_heating_target(HEATING_TARGET_MIN);
                        if (modbus_write_register(thread_client, REG_DHW_PRIORITY, 1) == 0) {
                            current_priority = PRIORITY_DHW;
                            printf("Control loop: DHW-only mode, set DHW priority\n");
                        }
                        break;
                    case 2:  // Heating-only: drop DHW target to minimum, clear DHW priority
                        printf("Control loop: Heating-only mode, setting DHW target to min (%.1f)\n", DHW_TARGET_MIN);
                        set_dhw_target(DHW_TARGET_MIN);
                        if (modbus_write_register(thread_client, REG_DHW_PRIORITY, 0) == 0) {
                            current_priority = PRIORITY_HEATING;
                            printf("Control loop: Heating-only mode, cleared DHW priority\n");
                        }
                        break;
                    case 3:  // DHW+Heating: restore user's saved targets, set DHW priority
                        printf("Control loop: DHW+Heating mode, restoring targets (DHW=%.1f, Heating=%.1f)\n",
                               saved_dhw_target, saved_heating_target);
                        set_dhw_target(saved_dhw_target);
                        set_heating_target(saved_heating_target);
                        if (modbus_write_register(thread_client, REG_DHW_PRIORITY, 1) == 0) {
                            current_priority = PRIORITY_DHW;
                            printf("Control loop: DHW+Heating mode, set DHW priority\n");
                        }
                        break;
                    case 0:  // Off: no target or priority changes needed
                        break;
                }
                break;
                
            default:
                fprintf(stderr, "Control loop: Unknown command type: %d\n", cmd.type);
                break;
        }
    }
    if (cmd_count == 0) {
        // No commands to process - this is normal, just silent
    } else {
        printf("Control loop: Batch complete - processed %d command(s)\n", cmd_count);
        // Signal that commands were processed, wake up main loop if sleeping
        pthread_mutex_lock(&kick_mutex);
        kicked = true;
        pthread_cond_signal(&kick_cond);
        pthread_mutex_unlock(&kick_mutex);
    }
}

// Apply control logic based on current state
// Respects the user's desired working mode:
// - desired_working_mode 0 = OFF: keep device in MODE_SET_OFF
// - desired_working_mode 1 = DHW only: keep device in MODE_SET_HEAT_DHW (device limitation)
// - desired_working_mode 2 = Heating only: keep device in MODE_SET_HEAT_DHW (device limitation)
// - desired_working_mode 3 = DHW+Heating: keep device in MODE_SET_HEAT_DHW
// We only set the mode if it differs from current_mode (to avoid spamming).
static void apply_control_logic(status_snapshot_t *status) {
    if (!status) {
        return;
    }
    
    // Determine what device mode we should be in
    int desired_device_mode;
    switch (desired_working_mode) {
        case 0:  desired_device_mode = MODE_SET_OFF;        break;  // Off
        case 1:  desired_device_mode = MODE_SET_HEAT_DHW;   break;  // DHW only (device limitation)
        case 2:  desired_device_mode = MODE_SET_HEAT_DHW;   break;  // Heating only (device limitation)
        case 3:  desired_device_mode = MODE_SET_HEAT_DHW;   break;  // DHW + Heating
        default: desired_device_mode = MODE_SET_HEAT_DHW;   break;  // Fallback
    }
    
    // Only change mode if it doesn't match
    if (current_mode != desired_device_mode) {
        printf("Control loop: Correcting device mode from %d to %d (desired working mode=%d)\n",
               current_mode, desired_device_mode, desired_working_mode);
        if (set_running_mode(desired_device_mode) == 0) {
            current_mode = desired_device_mode;
        }
    }
    
    // Enforce target temperature overrides based on working mode
    // This ensures targets stay correct even if read-back drifts
    switch (desired_working_mode) {
        case 1:  // DHW-only: keep heating target at minimum
            if (status->heating_target > HEATING_TARGET_MIN + 0.5f) {
                printf("Control loop: DHW-only enforcing heating target min (%.1f, was %.1f)\n",
                       HEATING_TARGET_MIN, status->heating_target);
                set_heating_target(HEATING_TARGET_MIN);
            }
            break;
        case 2:  // Heating-only: keep DHW target at minimum
            if (status->dhw_target > DHW_TARGET_MIN + 0.5f) {
                printf("Control loop: Heating-only enforcing DHW target min (%.1f, was %.1f)\n",
                       DHW_TARGET_MIN, status->dhw_target);
                set_dhw_target(DHW_TARGET_MIN);
            }
            break;
        case 3:  // DHW+Heating: ensure user's saved targets are active
            // Only correct if significantly different (avoid spamming)
            if (fabsf(status->dhw_target - saved_dhw_target) > 0.5f) {
                printf("Control loop: Restoring DHW target (%.1f, was %.1f)\n",
                       saved_dhw_target, status->dhw_target);
                set_dhw_target(saved_dhw_target);
            }
            if (fabsf(status->heating_target - saved_heating_target) > 0.5f) {
                printf("Control loop: Restoring heating target (%.1f, was %.1f)\n",
                       saved_heating_target, status->heating_target);
                set_heating_target(saved_heating_target);
            }
            break;
    }
    
    // Enforce priority based on working mode
    switch (desired_working_mode) {
        case 1:  // DHW-only: must have DHW priority
        case 3:  // DHW+Heating: must have DHW priority
            if (current_priority != PRIORITY_DHW) {
                printf("Control loop: Mode %d enforcing DHW priority\n", desired_working_mode);
                if (modbus_write_register(thread_client, REG_DHW_PRIORITY, 1) == 0) {
                    current_priority = PRIORITY_DHW;
                }
            }
            break;
        case 2:  // Heating-only: must have no DHW priority
            if (current_priority != PRIORITY_HEATING) {
                printf("Control loop: Heating-only mode clearing DHW priority\n");
                if (modbus_write_register(thread_client, REG_DHW_PRIORITY, 0) == 0) {
                    current_priority = PRIORITY_HEATING;
                }
            }
            break;
    }
}

// Main control loop thread function
static void *control_loop_thread_func(void *arg) {
    (void)arg;
    
    printf("Control loop thread started\n");
    
    // Publish initial status snapshot immediately so web server has data
    {
        status_snapshot_t initial_status;
        memset(&initial_status, 0, sizeof(initial_status));
        if (read_status(&initial_status)) {
            initial_status.device_online = true;
            initial_status.working_mode = desired_working_mode;
            spsc_push_status_snapshot_t(thread_status_queue, initial_status);
        }
    }
    
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
            
            // Set working mode for status reporting
            status.working_mode = desired_working_mode;
            
            // Publish status to queue
            if (!spsc_push_status_snapshot_t(thread_status_queue, status)) {
                fprintf(stderr, "Control loop: Status queue full, dropping snapshot\n");
            }
        } else {
            fprintf(stderr, "Control loop: Failed to read status\n");
            // Publish offline status
            status.device_online = false;
            status.working_mode = desired_working_mode;
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
            // Wait with timeout, but can be woken up early if kicked
            pthread_mutex_lock(&kick_mutex);
            kicked = false;
            struct timespec wait_time;
            clock_gettime(CLOCK_MONOTONIC, &wait_time);
            long wait_sec = sleep_ms / 1000;
            long wait_nsec = (sleep_ms % 1000) * 1000000;
            wait_time.tv_sec += wait_sec;
            wait_time.tv_nsec += wait_nsec;
            if (wait_time.tv_nsec >= 1000000000) {
                wait_time.tv_sec++;
                wait_time.tv_nsec -= 1000000000;
            }
            while (!kicked && !stop_requested) {
                int ret = pthread_cond_timedwait(&kick_cond, &kick_mutex, &wait_time);
                if (ret == ETIMEDOUT) break;
            }
            pthread_mutex_unlock(&kick_mutex);
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
                       spsc_status_snapshot_t_queue_t *status_queue,
                       bool run_selftest) {
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
    
    // Run self-test if requested (only during startup, not in normal operation)
    if (run_selftest) {
        selftest_report_t selftest_result = selftest_run(client);
        printf("Self-test: %d/%d registers passed\n", selftest_result.passed, selftest_result.total);
        
        if (!selftest_result.all_critical_passed) {
            fprintf(stderr, "Self-test failed: critical registers did not pass\n");
            selftest_print_report(&selftest_result);
            modbus_client_disconnect(client);
            return -1;
        }
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
