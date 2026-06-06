#include "web_server.h"
#include "config.h"
#include "spsc_queue.h"

#include <mongoose.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

static struct mg_mgr mgr;
static bool g_running = false;
static volatile sig_atomic_t g_shutting_down = 0;
static spsc_cmd_t_queue_t *g_cmd_queue = NULL;
static spsc_status_snapshot_t_queue_t *g_status_queue = NULL;
static status_snapshot_t g_last_status;

static const char *mode_to_string(int mode) {
    // Values from REG_RUNNING_MODE (0x002C), the writable mode register
    // 0=Off, 1=Cool+DHW, 2=Heat+DHW
    switch (mode) {
    case 0:  return "off";
    case 1:  return "cool+dhw";
    case 2:  return "heat+dhw";
    default: return "unknown";
    }
}

static const char *status_to_string(int status) {
    // Values from REG_RUNNING_STATUS (0x002D), the read-only status register
    // 0=Off, 1=Cool, 2=Heat, 4=DHW, 7=Defrost, 20=Home anti-freeze
    switch (status) {
    case 0:  return "off";
    case 1:  return "cool";
    case 2:  return "heat";
    case 4:  return "dhw";
    case 7:  return "defrost";
    case 20: return "antifreeze";
    default: return "unknown";
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

    char response[4096];
    snprintf(response, sizeof(response),
        "{\"dhwTemperature\":%.1f,"
        "\"dhwTarget\":%.1f,"
        "\"heatingTemperature\":%.1f,"
        "\"heatingTarget\":%.1f,"
        "\"outdoorTemperature\":%.1f,"
        "\"leavingWaterTemperature\":%.1f,"
        "\"enteringWaterTemperature\":%.1f,"
        "\"mode\":\"%s\","
        "\"runningStatus\":\"%s\","
        "\"priority\":\"%s\","
        "\"status\":\"%s\","
        "\"deviceOnline\":%s,"
        "\"acCurrent\":%.2f,"
        "\"dcCurrent\":%.2f,"
        "\"acVoltage\":%.1f,"
        "\"dcVoltage\":%.1f,"
        "\"acPowerVA\":%.1f,"
        "\"acPowerW\":%.1f,"
        "\"powerValid\":%s,"
        "\"compressorFrequency\":%.1f,"
        "\"waterFlow\":%.2f,"
        "\"unitCapacityKw\":%d,"
        "\"actualCapacityOutput\":%d,"
        "\"oduInputStatus\":%d,"
        "\"compressorRuntimeHours\":%d,"
        "\"pumpRuntimeHours\":%d,"
        "\"heatOutputW\":%.1f,"
        "\"cop\":%.2f,"
        "\"copValid\":%s,"
        "\"workingMode\":%d}\n",
        g_last_status.dhw_tank_temp,
        g_last_status.dhw_target,
        g_last_status.leaving_water_temp,
        g_last_status.heating_target,
        g_last_status.outdoor_temp,
        g_last_status.leaving_water_temp,
        g_last_status.entering_water_temp,
        mode_to_string(g_last_status.running_mode),
        status_to_string(g_last_status.running_status),
        g_last_status.dhw_priority ? "dhw" : "heating",
        g_last_status.is_running ? "running" : "stopped",
        g_last_status.device_online ? "true" : "false",
        g_last_status.ac_current,
        g_last_status.dc_current,
        g_last_status.ac_voltage,
        g_last_status.dc_voltage,
        g_last_status.ac_power_va,
        g_last_status.ac_power_w,
        g_last_status.power_valid ? "true" : "false",
        g_last_status.compressor_freq,
        g_last_status.water_flow,
        g_last_status.unit_capacity_kw,
        g_last_status.actual_capacity_output,
        g_last_status.odu_input_status,
        g_last_status.compressor_runtime_h,
        g_last_status.pump_runtime_h,
        g_last_status.heat_output_w,
        g_last_status.cop,
        g_last_status.cop_valid ? "true" : "false",
        g_last_status.working_mode
    );

    send_json_reply(c, 200, response);
}

static void api_set_dhw_handler(struct mg_connection *c, struct mg_str *body) {
    if (web_server_is_shutting_down()) {
        send_json_reply(c, 503, "{\"error\":\"Server is shutting down\"}");
        return;
    }

    if (body->len == 0) {
        send_json_reply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    double temperature = 0;
    if (mg_json_get_num(*body, "$.temperature", &temperature) < 1) {
        send_json_reply(c, 400, "{\"error\":\"Missing or invalid temperature\"}");
        return;
    }

    if (temperature < DHW_TEMP_MIN || temperature > DHW_TEMP_MAX) {
        send_json_reply(c, 422, "{\"error\":\"Temperature out of range\"}");
        return;
    }

    printf("Web server: Received DHW set request - temperature=%.1f°C (range: %.0f-%.0f)\n",
           temperature, DHW_TEMP_MIN, DHW_TEMP_MAX);

    cmd_t cmd = {
        .type = CMD_SET_DHW_TEMP,
        .float_val = (float)temperature,
        .int_val = 0
    };
    bool pushed = spsc_push_cmd_t(g_cmd_queue, cmd);
    printf("Web server: DHW command pushed to queue (temp=%.1f, pushed=%d)\n", temperature, pushed);
    send_json_reply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

static void api_set_heating_handler(struct mg_connection *c, struct mg_str *body) {
    if (web_server_is_shutting_down()) {
        send_json_reply(c, 503, "{\"error\":\"Server is shutting down\"}");
        return;
    }

    if (body->len == 0) {
        send_json_reply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    double temperature = 0;
    if (mg_json_get_num(*body, "$.temperature", &temperature) < 1) {
        send_json_reply(c, 400, "{\"error\":\"Missing or invalid temperature\"}");
        return;
    }

    if (temperature < HEATING_TEMP_MIN || temperature > HEATING_TEMP_MAX) {
        send_json_reply(c, 422, "{\"error\":\"Temperature out of range\"}");
        return;
    }

    printf("Web server: Received heating set request - temperature=%.1f°C (range: %.0f-%.0f)\n",
           temperature, HEATING_TEMP_MIN, HEATING_TEMP_MAX);

    cmd_t cmd = {
        .type = CMD_SET_HEATING_TEMP,
        .float_val = (float)temperature,
        .int_val = 0
    };
    bool pushed = spsc_push_cmd_t(g_cmd_queue, cmd);
    printf("Web server: Heating command pushed to queue (temp=%.1f, pushed=%d)\n", temperature, pushed);
    send_json_reply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

static void api_set_priority_handler(struct mg_connection *c, struct mg_str *body) {
    if (web_server_is_shutting_down()) {
        send_json_reply(c, 503, "{\"error\":\"Server is shutting down\"}");
        return;
    }

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

    printf("Web server: Received priority set request - priority=%s (pri_val=%d)\n",
           priority, pri_val);
    free(priority);
    
    cmd_t cmd = {
        .type = CMD_SET_PRIORITY,
        .float_val = 0.0f,
        .int_val = pri_val
    };
    bool pushed = spsc_push_cmd_t(g_cmd_queue, cmd);
    printf("Web server: Priority command pushed to queue (pri_val=%d, pushed=%d)\n", pri_val, pushed);
    send_json_reply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

static void api_set_mode_handler(struct mg_connection *c, struct mg_str *body) {
    if (web_server_is_shutting_down()) {
        send_json_reply(c, 503, "{\"error\":\"Server is shutting down\"}");
        return;
    }

    if (body->len == 0) {
        send_json_reply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }
    
    // Parse mode (0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating)
    long mode = mg_json_get_long(*body, "$.mode", -1);
    if (mode == -1) {
        send_json_reply(c, 400, "{\"error\":\"Missing mode parameter\"}");
        return;
    }
    
    // Parse priority (optional, defaults to DHW)
    long priority = mg_json_get_long(*body, "$.priority", 1); // Default to DHW priority
    
    printf("Web server: Received mode set request - mode=%ld, priority=%ld\n", mode, priority);
    
    // Map working mode to device mode:
    // 0 (Off) -> Mode 0 (Off)
    // 1 (DHW-only) -> Mode 0 (Off) + set heating target very high
    // 2 (Heating-only) -> Mode 2 (Heat+DHW) + set DHW target very high
    // 3 (DHW+Heating) -> Mode 2 (Heat+DHW)
    
    cmd_t mode_cmd = {
        .type = CMD_SET_RUNNING_MODE,
        .int_val = mode
    };
    
    // If DHW-only, also queue a command to disable heating
    // If Heating-only, also queue a command to disable DHW
    
    bool pushed = spsc_push_cmd_t(g_cmd_queue, mode_cmd);
    printf("Web server: Mode command pushed to queue (mode=%ld, pushed=%d)\n", mode, pushed);
    
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
    } else if (mg_strcmp(hm->uri, mg_str("/api/set-mode")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            api_set_mode_handler(c, &hm->body);
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
    g_shutting_down = 0;

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
        mg_mgr_poll(&mgr, 100);
    }
    mg_mgr_free(&mgr);
}

void web_server_stop(void) {
    g_shutting_down = 1;
    g_running = false;
}

bool web_server_is_shutting_down(void) {
    return g_shutting_down != 0;
}
