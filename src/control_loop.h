#ifndef CONTROL_LOOP_H
#define CONTROL_LOOP_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_client.h"
#include "spsc_queue.h"

// Start the control loop thread (connects, polls, runs control loop)
// Reads from cmd_queue, writes to status_queue
// run_selftest: if true, run self-test before starting; if false, skip self-test
// Returns 0 on success, -1 on failure
int control_loop_start(modbus_client_t *client,
                       spsc_cmd_t_queue_t *cmd_queue,
                       spsc_status_snapshot_t_queue_t *status_queue,
                       bool run_selftest);

// Stop the control loop thread
void control_loop_stop(void);

// Check if control loop thread is running
bool control_loop_is_running(void);

#endif // CONTROL_LOOP_H
