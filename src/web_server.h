#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "spsc_queue.h"

int web_server_init(int port, const char *static_dir,
                    spsc_cmd_t_queue_t *cmd_queue,
                    spsc_status_snapshot_t_queue_t *status_queue);

void web_server_run(void);

void web_server_stop(void);

// Check if server is shutting down
bool web_server_is_shutting_down(void);

#endif // WEB_SERVER_H
