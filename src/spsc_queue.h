#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Single-producer single-consumer lock-free ring buffer
// Producer calls spsc_push(), consumer calls spsc_pop()
// Only safe for ONE producer thread and ONE consumer thread

#define SPSC_QUEUE_DECLARE(TYPE, SIZE) \
    typedef struct { \
        TYPE buf[SIZE]; \
        _Alignas(64) volatile uint32_t head; \
        _Alignas(64) volatile uint32_t tail; \
    } spsc_##TYPE##_queue_t

#define SPSC_PUSH(q, item) spsc_push_##TYPE((q), (item))
#define SPSC_POP(q, item)  spsc_pop_##TYPE((q), (item))

// Generic SPSC queue for cmd_t
typedef enum {
    CMD_SET_DHW_TEMP,
    CMD_SET_HEATING_TEMP,
    CMD_SET_PRIORITY,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    float float_val;
    int int_val;
} cmd_t;

SPSC_QUEUE_DECLARE(cmd_t, 16);

static inline bool spsc_push_cmd_t(spsc_cmd_t_queue_t *q, cmd_t item) {
    uint32_t next_head = (q->head + 1) % 16;
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
    q->tail = (q->tail + 1) % 16;
    return true;
}

// Generic SPSC queue for status_snapshot_t
typedef struct {
    float outdoor_temp;
    float indoor_temp;
    float leaving_water_temp;
    float dhw_tank_temp;
    float dhw_target;
    float heating_target;
    int running_mode;
    bool dhw_priority;
    bool is_running;
    bool device_online;
} status_snapshot_t;

SPSC_QUEUE_DECLARE(status_snapshot_t, 4);

static inline bool spsc_push_status_snapshot_t(spsc_status_snapshot_t_queue_t *q, status_snapshot_t item) {
    uint32_t next_head = (q->head + 1) % 4;
    if (next_head == q->tail) {
        return false;
    }
    q->buf[q->head] = item;
    __asm__ __volatile__("" ::: "memory");
    q->head = next_head;
    return true;
}

static inline bool spsc_pop_status_snapshot_t(spsc_status_snapshot_t_queue_t *q, status_snapshot_t *item) {
    if (q->tail == q->head) {
        return false;
    }
    *item = q->buf[q->tail];
    __asm__ __volatile__("" ::: "memory");
    q->tail = (q->tail + 1) % 4;
    return true;
}

// Drain all items, keeping only the latest
static inline bool spsc_latest_status_snapshot_t(spsc_status_snapshot_t_queue_t *q, status_snapshot_t *item) {
    bool found = false;
    status_snapshot_t tmp;
    while (spsc_pop_status_snapshot_t(q, &tmp)) {
        *item = tmp;
        found = true;
    }
    return found;
}

#endif // SPSC_QUEUE_H
