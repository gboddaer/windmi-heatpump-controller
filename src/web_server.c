#include "web_server.h"
#include "config.h"
#include "spsc_queue.h"

#include <mongoose.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static struct mg_mgr mgr;
static bool g_running = false;
static spsc_cmd_t_queue_t *g_cmd_queue = NULL;
static spsc_status_snapshot_t_queue_t *g_status_queue = NULL;
static status_snapshot_t g_last_status;

static const char *mode_to_string(int mode) {
    switch (mode) {
    case MODE_OFF:              return "off";
    case MODE_COOL:             return "cool";
    case MODE_HEAT:             return "heat";
    case MODE_DHW:              return "dhw";
    case MODE_DEFROST:          return "defrost";
    case MODE_HOME_ANTIFREEZE:  return "antifreeze";
    default:                    return "unknown";
    }
}

static void send_json_reply(struct mg_connection *c, int status_code,
                             const char *json) {
    mg_http_reply(c, status_code,
                  "Content-Type: application/json\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Connection: close\r\n",
                  "%s", json);
}

static void api_status_handler(struct mg_connection *c) {
    status_snapshot_t snapshot;

    if (spsc_latest_status_snapshot_t(g_status_queue, &snapshot)) {
        g_last_status = snapshot;
    }

    char response[1024];
    snprintf(response, sizeof(response),
        "{\"dhwTemperature\":%.1f,"
        "\"dhwTarget\":%.1f,"
        "\"heatingTemperature\":%.1f,"
        "\"heatingTarget\":%.1f,"
        "\"outdoorTemperature\":%.1f,"
        "\"leavingWaterTemperature\":%.1f,"
        "\"mode\":\"%s\","
        "\"priority\":\"%s\","
        "\"status\":\"%s\","
        "\"deviceOnline\":%s}\n",
        g_last_status.dhw_tank_temp,
        g_last_status.dhw_target,
        g_last_status.leaving_water_temp,
        g_last_status.heating_target,
        g_last_status.outdoor_temp,
        g_last_status.leaving_water_temp,
        mode_to_string(g_last_status.running_mode),
        g_last_status.dhw_priority ? "dhw" : "heating",
        g_last_status.is_running ? "running" : "stopped",
        g_last_status.device_online ? "true" : "false"
    );

    send_json_reply(c, 200, response);
}

static void api_set_dhw_handler(struct mg_connection *c, struct mg_str *body) {
    if (body->len == 0) {
        send_json_reply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    double temperature = 0;
    if (mg_json_get_num(*body, "$.temperature", &temperature) == 0) {
        send_json_reply(c, 400, "{\"error\":\"Missing or invalid temperature\"}");
        return;
    }

    if (temperature < DHW_TEMP_MIN || temperature > DHW_TEMP_MAX) {
        send_json_reply(c, 422, "{\"error\":\"Temperature out of range\"}");
        return;
    }

    cmd_t cmd = {
        .type = CMD_SET_DHW_TEMP,
        .float_val = (float)temperature,
        .int_val = 0
    };
    spsc_push_cmd_t(g_cmd_queue, cmd);
    send_json_reply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

static void api_set_heating_handler(struct mg_connection *c, struct mg_str *body) {
    if (body->len == 0) {
        send_json_reply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    double temperature = 0;
    if (mg_json_get_num(*body, "$.temperature", &temperature) == 0) {
        send_json_reply(c, 400, "{\"error\":\"Missing or invalid temperature\"}");
        return;
    }

    if (temperature < HEATING_TEMP_MIN || temperature > HEATING_TEMP_MAX) {
        send_json_reply(c, 422, "{\"error\":\"Temperature out of range\"}");
        return;
    }

    cmd_t cmd = {
        .type = CMD_SET_HEATING_TEMP,
        .float_val = (float)temperature,
        .int_val = 0
    };
    spsc_push_cmd_t(g_cmd_queue, cmd);
    send_json_reply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

static void api_set_priority_handler(struct mg_connection *c, struct mg_str *body) {
    if (body->len == 0) {
        send_json_reply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    char *priority = mg_json_get_str(*body, "$.priority");
    if (priority == NULL) {
        send_json_reply(c, 400, "{\"error\":\"Missing or invalid priority\"}");
        return;
    }

    int pri_val;
    if (strncmp(priority, "dhw", 3) == 0) {
        pri_val = 1;
    } else if (strncmp(priority, "heating", 7) == 0) {
        pri_val = 0;
    } else {
        free(priority);
        send_json_reply(c, 422, "{\"error\":\"Invalid priority value\"}");
        return;
    }

    free(priority);

    cmd_t cmd = {
        .type = CMD_SET_PRIORITY,
        .float_val = 0.0f,
        .int_val = pri_val
    };
    spsc_push_cmd_t(g_cmd_queue, cmd);
    send_json_reply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

static void http_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;

    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    if (mg_strcmp(hm->uri, mg_str("/api/status")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("GET")) == 0) {
            api_status_handler(c);
        } else {
            send_json_reply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/set-dhw")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            api_set_dhw_handler(c, &hm->body);
        } else {
            send_json_reply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/set-heating")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            api_set_heating_handler(c, &hm->body);
        } else {
            send_json_reply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/set-priority")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            api_set_priority_handler(c, &hm->body);
        } else {
            send_json_reply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else {
        struct mg_http_serve_opts opts = {.root_dir = "static"};
        mg_http_serve_dir(c, hm, &opts);
    }
}

int web_server_init(int port, const char *static_dir,
                    spsc_cmd_t_queue_t *cmd_queue,
                    spsc_status_snapshot_t_queue_t *status_queue) {
    g_cmd_queue = cmd_queue;
    g_status_queue = status_queue;
    memset(&g_last_status, 0, sizeof(g_last_status));

    mg_mgr_init(&mgr);

    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d", WEB_SERVER_IP, port);

    struct mg_connection *conn = mg_http_listen(&mgr, url, http_handler, NULL);
    if (conn == NULL) {
        fprintf(stderr, "Failed to start server on %s\n", url);
        return -1;
    }

    printf("Web server started on %s\n", url);
    printf("Static files served from: %s\n", static_dir);

    g_running = true;
    return 0;
}

void web_server_run(void) {
    while (g_running) {
        mg_mgr_poll(&mgr, 1000);
    }
}

void web_server_stop(void) {
    g_running = false;
    mg_mgr_free(&mgr);
}
