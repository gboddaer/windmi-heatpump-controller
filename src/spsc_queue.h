#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Single-producer single-consumer lock-free ring buffer
// Producer calls spsc_push(), consumer calls spsc_pop()
// Only safe for ONE producer thread and ONE consumer thread

// ---- Type definitions ----

typedef enum {
    CMD_SET_DHW_TEMP,
    CMD_SET_HEATING_TEMP,
    CMD_SET_PRIORITY,
    CMD_SET_RUNNING_MODE,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    float float_val;
    int int_val;
} cmd_t;

typedef struct {
    float outdoor_temp;
    float indoor_temp;
    float leaving_water_temp;
    float entering_water_temp;
    float dhw_tank_temp;
    float dhw_target;
    float heating_target;
    int running_mode;        // From REG_RUNNING_MODE (0x002C): 0=Off, 1=Cool+DHW, 2=Heat+DHW
    int running_status;       // From REG_RUNNING_STATUS (0x002D): 0=Off, 1=Cool, 2=Heat, 4=DHW, 7=Defrost, 20=Anti-freeze
    bool dhw_priority;
    bool is_running;
    bool device_online;
    // Power monitoring
    float ac_current;      // AC current in Amps (raw * 2)
    float dc_current;      // DC current in Amps (raw * 4)
    float ac_voltage;      // AC voltage in Volts (raw)
    float dc_voltage;      // DC voltage in Volts (raw / 2)
    float ac_power_va;     // AC apparent power in VA (ac_voltage * ac_current)
    float ac_power_w;      // AC real power in Watts (estimated: VA * power_factor)
    bool power_valid;      // True if both AC current and AC voltage were read
    // Diagnostic registers
    float compressor_freq;
    float water_flow;
    int unit_capacity_kw;
    int actual_capacity_output;
    int odu_input_status;
    int compressor_runtime_h;
    int pump_runtime_h;
    // COP estimation
    float heat_output_w;
    float cop;
    bool cop_valid;
    // Working mode (0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating)
    int working_mode;
} status_snapshot_t;

// ---- Queue sizes ----
#define CMD_QUEUE_SIZE         16
#define STATUS_QUEUE_SIZE      32

// ---- Queue struct definitions ----

typedef struct {
    cmd_t buf[CMD_QUEUE_SIZE];
    _Alignas(64) volatile uint32_t head;
    _Alignas(64) volatile uint32_t tail;
} spsc_cmd_t_queue_t;

typedef struct {
    status_snapshot_t buf[STATUS_QUEUE_SIZE];
    _Alignas(64) volatile uint32_t head;
    _Alignas(64) volatile uint32_t tail;
    _Alignas(64) volatile uint32_t write_count;  // For ring buffer overwrite
} spsc_status_snapshot_t_queue_t;

// ---- cmd_t queue operations ----

static inline bool spsc_push_cmd_t(spsc_cmd_t_queue_t *q, cmd_t item) {
    uint32_t next_head = (q->head + 1) % CMD_QUEUE_SIZE;
    if (next_head == q->tail) {
        return false; // full
    }
    q->buf[q->head] = item;
    __asm__ __volatile__("" ::: "memory"); // store-store barrier
    q->head = next_head;
    return true;
}

static inline bool spsc_pop_cmd_t(spsc_cmd_t_queue_t *q, cmd_t *item) {
    if (q->tail == q->head) {
        return false; // empty
    }
    *item = q->buf[q->tail];
    __asm__ __volatile__("" ::: "memory"); // load-load barrier
    q->tail = (q->tail + 1) % CMD_QUEUE_SIZE;
    return true;
}

// ---- status_snapshot_t queue operations (ring buffer with overwrite) ----

static inline void spsc_push_status_snapshot_t(spsc_status_snapshot_t_queue_t *q, status_snapshot_t item) {
    // Ring buffer: always write, overwrite oldest if full
    uint32_t write_count = q->write_count++;
    uint32_t buf_idx = write_count % STATUS_QUEUE_SIZE;
    
    q->buf[buf_idx] = item;
    __asm__ __volatile__("" ::: "memory");
    
    // Update tail to point past this write
    q->tail = write_count + 1;
    
    // If we overwrote old data, advance head to maintain at least one slot
    uint32_t current_head = q->head;
    uint32_t current_tail = write_count + 1;
    
    // Ensure head doesn't lag too far behind (keep at most STATUS_QUEUE_SIZE-1 items)
    if (current_tail - current_head > STATUS_QUEUE_SIZE - 1) {
        q->head = current_tail - (STATUS_QUEUE_SIZE - 1);
    }
}

static inline bool spsc_pop_status_snapshot_t(spsc_status_snapshot_t_queue_t *q, status_snapshot_t *item) {
    uint32_t current_head = q->head;
    uint32_t current_tail = q->tail;
    
    if (current_head >= current_tail) {
        return false;  // Empty
    }
    
    uint32_t buf_idx = current_head % STATUS_QUEUE_SIZE;
    *item = q->buf[buf_idx];
    __asm__ __volatile__("" ::: "memory");
    q->head = current_head + 1;
    return true;
}

// Get the latest snapshot without consuming
static inline bool spsc_latest_status_snapshot_t(spsc_status_snapshot_t_queue_t *q, status_snapshot_t *item) {
    uint32_t current_tail = q->tail;
    uint32_t current_head = q->head;
    
    if (current_head >= current_tail) {
        return false;  // Empty
    }
    
    // Get the last written item (tail - 1)
    uint32_t last_idx = current_tail - 1;
    uint32_t buf_idx = last_idx % STATUS_QUEUE_SIZE;
    *item = q->buf[buf_idx];
    return true;
}

#endif // SPSC_QUEUE_H