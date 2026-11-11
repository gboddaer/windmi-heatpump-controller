/**
 * @file src/web/WebServer.cpp
 * @brief Web server C++ implementation matching master branch web_server.c
 */

#include "web/WebServer.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include "utils/ScheduleConfig.hpp"
#include "core/ModeNames.hpp"
#include "config.h"

#include <mongoose.h>
#undef poll  // Mongoose defines poll(a,b,c) as WSAPoll on Windows
#include <cstdio>

#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <sstream>

namespace windmi {

// ---- Static helper functions (matching master) ----

static const char* status_to_string(int status) {
    switch (status) {
        case MODE_STATUS_OFF:        return "off";
        case MODE_STATUS_COOL:       return "cool";
        case MODE_STATUS_HEAT:       return "heat";
        case MODE_STATUS_DHW:        return "dhw";
        case MODE_STATUS_DEFROST:    return "defrost";
        case MODE_STATUS_ANTIFREEZE: return "antifreeze";
        default:                     return "unknown";
    }
}

// ---- WebServer implementation ----

WebServer::WebServer(int port, const std::string& static_dir,
                     CmdQueue* cmd_queue, StatusQueue* status_queue,
                     Scheduler* scheduler,
                     std::function<void()> wake_callback)
    : static_dir_(static_dir)
    , cmd_queue_(cmd_queue)
    , status_queue_(status_queue)
    , scheduler_(scheduler)
    , last_status_{}
    , running_(false)
    , shutting_down_(0)
    , mgr_freed_(false)
    , wake_callback_(std::move(wake_callback))
{
    mg_mgr_init(&mgr_);

    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d", WEB_SERVER_IP, port);

    struct mg_connection* conn = mg_http_listen(&mgr_, url, WebServer::eventHandler, this);
    if (!conn) {
        WINDMI_LOG_ERROR(LOG_TAG_WEBSERVER, "Failed to start server on %s", url);
        mg_mgr_free(&mgr_);
        mgr_freed_ = true;
        throw std::runtime_error("Failed to start web server");
    }

    WINDMI_LOG_INFO(LOG_TAG_WEBSERVER, "Started on %s", url);
    WINDMI_LOG_INFO(LOG_TAG_WEBSERVER, "Static files served from: %s", static_dir_.c_str());

    running_.store(true);
}

WebServer::~WebServer() {
    stop();
    if (!mgr_freed_) {
        mg_mgr_free(&mgr_);
        mgr_freed_ = true;
    }
}

void WebServer::run() {
    while (running_.load()) {
        pollOnce(100);
    }
}

void WebServer::pollOnce(int timeout_ms) {
    if (running_.load()) {
        mg_mgr_poll(&mgr_, timeout_ms);
    }
}

void WebServer::stop() {
    shutting_down_ = 1;
    running_.store(false);
}

bool WebServer::isShuttingDown() const {
    return shutting_down_ != 0;
}

void WebServer::sendJsonReply(struct mg_connection* c, int status_code,
                               const char* json) {
    mg_http_reply(c, status_code,
                  "Content-Type: application/json\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Connection: close\r\n",
                  "%s", json);
}

// ---- Static event handler (mongoose callback) ----

void WebServer::eventHandler(struct mg_connection* c, int ev, void* ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;

    auto* self = static_cast<WebServer*>(c->fn_data);
    if (!self) return;

    auto* hm = static_cast<struct mg_http_message*>(ev_data);
    self->handleRequest(c, hm);
}

// ---- HTTP request routing ----

void WebServer::handleRequest(struct mg_connection* c, struct mg_http_message* hm) {
    if (mg_strcmp(hm->uri, mg_str("/api/status")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("GET")) == 0) {
            apiStatusHandler(c);
        } else {
            sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/set-dhw")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            apiSetDhwHandler(c, &hm->body);
        } else {
            sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/set-heating")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            apiSetHeatingHandler(c, &hm->body);
        } else {
            sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/set-priority")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            apiSetPriorityHandler(c, &hm->body);
        } else {
            sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/set-mode")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            apiSetModeHandler(c, &hm->body);
        } else {
            sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/set-occupancy")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            apiSetOccupancyHandler(c, &hm->body);
        } else {
            sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/schedule")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("GET")) == 0) {
            apiScheduleGetHandler(c);
        } else if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            apiScheduleSetHandler(c, &hm->body);
        } else {
            sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/schedule/toggle")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            apiScheduleToggleHandler(c, &hm->body);
        } else {
            sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else if (mg_strcmp(hm->uri, mg_str("/api/schedule/resume")) == 0) {
        if (mg_strcasecmp(hm->method, mg_str("POST")) == 0) {
            apiScheduleResumeHandler(c);
        } else {
            sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
        }
    } else {
        // Serve static files
        struct mg_http_serve_opts opts;
        memset(&opts, 0, sizeof(opts));
        opts.root_dir = static_dir_.c_str();
        mg_http_serve_dir(c, hm, &opts);
    }
}

// ---- API handlers (matching master branch web_server.c) ----

void WebServer::apiStatusHandler(struct mg_connection* c) {
    StatusSnapshot snapshot;
    if (status_queue_ && status_queue_->latest(snapshot)) {
        last_status_ = snapshot;
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

        "\"dhwValveStatus\":%d,"
        "\"dhwModeStatus\":%d,"
        "\"workingMode\":%d,"
        "\"occupancyMode\":%d,"
        "\"scheduleActive\":%s,"
        "\"schedulePaused\":%s}\n",
        last_status_.dhw_tank_temp,
        last_status_.dhw_target,
        last_status_.leaving_water_temp,
        last_status_.heating_target,
        last_status_.outdoor_temp,
        last_status_.leaving_water_temp,
        last_status_.entering_water_temp,
        mode_to_string(last_status_.running_mode),
        status_to_string(last_status_.running_status),
        last_status_.dhw_priority ? "dhw" : "heating",
        last_status_.is_running ? "running" : "stopped",
        last_status_.device_online ? "true" : "false",
        last_status_.ac_current,
        last_status_.dc_current,
        last_status_.ac_voltage,
        last_status_.dc_voltage,
        last_status_.ac_power_va,
        last_status_.ac_power_w,
        last_status_.power_valid ? "true" : "false",
        last_status_.compressor_freq,
        last_status_.water_flow,
        last_status_.unit_capacity_kw,
        last_status_.actual_capacity_output,
        last_status_.odu_input_status,
        last_status_.compressor_runtime_h,
        last_status_.pump_runtime_h,
        last_status_.heat_output_w,
        last_status_.cop,
        last_status_.cop_valid ? "true" : "false",
        last_status_.dhw_valve_status,
        last_status_.dhw_mode_status,
        last_status_.working_mode,
        last_status_.occupancy_mode,
        last_status_.schedule_active ? "true" : "false",
        last_status_.schedule_paused ? "true" : "false"
    );

    sendJsonReply(c, 200, response);
}

void WebServer::apiSetDhwHandler(struct mg_connection* c, struct mg_str* body) {
    if (isShuttingDown()) {
        sendJsonReply(c, 503, "{\"error\":\"Server is shutting down\"}");
        return;
    }

    if (body->len == 0) {
        sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    double temperature = 0;
    if (mg_json_get_num(*body, "$.temperature", &temperature) < 1) {
        sendJsonReply(c, 400, "{\"error\":\"Missing or invalid temperature\"}");
        return;
    }

    if (temperature < DHW_TEMP_MIN || temperature > DHW_TEMP_MAX) {
        sendJsonReply(c, 422, "{\"error\":\"Temperature out of range\"}");
        return;
    }

    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Received DHW set request - temperature=%.1f C (range: %.0f-%.0f)",
           temperature, static_cast<double>(DHW_TEMP_MIN), static_cast<double>(DHW_TEMP_MAX));

    Command cmd;
    cmd.type = CommandType::CMD_SET_DHW_TEMP;
    cmd.float_val = static_cast<float>(temperature);
    cmd.int_val = 0;
    bool pushed = cmd_queue_ ? cmd_queue_->push(cmd) : false;
    if (pushed && wake_callback_) wake_callback_();
    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "DHW command pushed to queue (temp=%.1f, pushed=%d)",
           temperature, pushed);
    sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

void WebServer::apiSetHeatingHandler(struct mg_connection* c, struct mg_str* body) {
    if (isShuttingDown()) {
        sendJsonReply(c, 503, "{\"error\":\"Server is shutting down\"}");
        return;
    }

    if (body->len == 0) {
        sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    double temperature = 0;
    if (mg_json_get_num(*body, "$.temperature", &temperature) < 1) {
        sendJsonReply(c, 400, "{\"error\":\"Missing or invalid temperature\"}");
        return;
    }

    if (temperature < HEATING_TEMP_MIN || temperature > HEATING_TEMP_MAX) {
        sendJsonReply(c, 422, "{\"error\":\"Temperature out of range\"}");
        return;
    }

    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Received heating set request - temperature=%.1f C (range: %.0f-%.0f)",
           temperature, static_cast<double>(HEATING_TEMP_MIN), static_cast<double>(HEATING_TEMP_MAX));

    Command cmd;
    cmd.type = CommandType::CMD_SET_HEATING_TEMP;
    cmd.float_val = static_cast<float>(temperature);
    cmd.int_val = 0;
    bool pushed = cmd_queue_ ? cmd_queue_->push(cmd) : false;
    if (pushed && wake_callback_) wake_callback_();
    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Heating command pushed to queue (temp=%.1f, pushed=%d)",
           temperature, pushed);
    sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

void WebServer::apiSetPriorityHandler(struct mg_connection* c, struct mg_str* body) {
    if (isShuttingDown()) {
        sendJsonReply(c, 503, "{\"error\":\"Server is shutting down\"}");
        return;
    }

    if (body->len == 0) {
        sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    char* priority = mg_json_get_str(*body, "$.priority");
    if (priority == nullptr) {
        sendJsonReply(c, 400, "{\"error\":\"Missing or invalid priority\"}");
        return;
    }

    int pri_val;
    if (strncmp(priority, "dhw", 3) == 0) {
        pri_val = 1;
    } else if (strncmp(priority, "heating", 7) == 0) {
        pri_val = 0;
    } else {
        free(priority);
        sendJsonReply(c, 422, "{\"error\":\"Invalid priority value\"}");
        return;
    }

    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Received priority set request - priority=%s (pri_val=%d)",
           priority, pri_val);
    free(priority);

    Command cmd;
    cmd.type = CommandType::CMD_SET_PRIORITY;
    cmd.float_val = 0.0f;
    cmd.int_val = pri_val;
    bool pushed = cmd_queue_ ? cmd_queue_->push(cmd) : false;
    if (pushed && wake_callback_) wake_callback_();
    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Priority command pushed to queue (pri_val=%d, pushed=%d)",
           pri_val, pushed);
    sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

void WebServer::apiSetModeHandler(struct mg_connection* c, struct mg_str* body) {
    if (isShuttingDown()) {
        sendJsonReply(c, 503, "{\"error\":\"Server is shutting down\"}");
        return;
    }

    if (body->len == 0) {
        sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    // Parse mode (0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating)
    long mode = mg_json_get_long(*body, "$.mode", -1);
    if (mode == -1) {
        sendJsonReply(c, 400, "{\"error\":\"Missing mode parameter\"}");
        return;
    }

    // Note: master also parses optional priority field (defaults to DHW=1)
    // but doesn't use it separately—it's handled by control_loop
    long priority = mg_json_get_long(*body, "$.priority", 1);

    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Received mode set request - mode=%ld, priority=%ld", mode, priority);

    Command mode_cmd;
    mode_cmd.type = CommandType::CMD_SET_RUNNING_MODE;
    mode_cmd.float_val = 0.0f;
    mode_cmd.int_val = static_cast<int>(mode);

    bool pushed = cmd_queue_ ? cmd_queue_->push(mode_cmd) : false;
    if (pushed && wake_callback_) wake_callback_();
    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Mode command pushed to queue (mode=%ld, pushed=%d)", mode, pushed);

    sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

void WebServer::apiSetOccupancyHandler(struct mg_connection* c, struct mg_str* body) {
    if (isShuttingDown()) {
        sendJsonReply(c, 503, "{\"error\":\"Server is shutting down\"}");
        return;
    }

    if (body->len == 0) {
        sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    // Parse occupancy mode (0=Away, 1=Sleep, 2=Home)
    long mode = mg_json_get_long(*body, "$.mode", -1);
    if (mode < 0 || mode > 2) {
        sendJsonReply(c, 422, "{\"error\":\"Invalid occupancy mode (0=Away, 1=Sleep, 2=Home)\"}");
        return;
    }

    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Received occupancy set request - mode=%ld", mode);

    Command cmd;
    cmd.type = CommandType::CMD_SET_OCCUPANCY_MODE;
    cmd.float_val = 0.0f;
    cmd.int_val = static_cast<int>(mode);
    bool pushed = cmd_queue_ ? cmd_queue_->push(cmd) : false;
    if (pushed && wake_callback_) wake_callback_();
    WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Occupancy command pushed to queue (mode=%ld, pushed=%d)", mode, pushed);
    sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

// ---- Schedule API handlers ----

static std::string rangesToJson(const std::vector<ScheduleRange>& ranges) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& r = ranges[i];
        oss << "{\"start\":" << r.startMinutes
            << ",\"end\":" << r.endMinutes
            << ",\"mode\":" << r.mode
            << ",\"heatingTemp\":" << r.heatingTemp
            << ",\"dhwTemp\":" << r.dhwTemp
            << "}";
    }
    oss << "]";
    return oss.str();
}

void WebServer::apiScheduleGetHandler(struct mg_connection* c) {
    if (!scheduler_) {
        sendJsonReply(c, 503, "{\"error\":\"Scheduler not available\"}");
        return;
    }

    std::ostringstream json;
    json << "{\"enabled\":" << (scheduler_->isEnabled() ? "true" : "false")
         << ",\"paused\":" << (scheduler_->isPaused() ? "true" : "false")
         << ",\"ranges\":" << rangesToJson(scheduler_->getRanges()) << "}";

    sendJsonReply(c, 200, json.str().c_str());
}

void WebServer::apiScheduleSetHandler(struct mg_connection* c, struct mg_str* body) {
    if (!scheduler_) {
        sendJsonReply(c, 503, "{\"error\":\"Scheduler not available\"}");
        return;
    }

    if (body->len == 0) {
        sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
        return;
    }

    // Parse enabled flag (accepts both JSON bool and JSON number)
    {
        bool enabledVal = false;
        if (mg_json_get_bool(*body, "$.enabled", &enabledVal) >= 1) {
            scheduler_->setEnabled(enabledVal);
        } else {
            double numVal = 0;
            if (mg_json_get_num(*body, "$.enabled", &numVal) >= 1) {
                scheduler_->setEnabled(numVal != 0);
            }
        }
    }

    // Parse ranges using hand-parsed approach (same as ScheduleConfig)
    // For now, delegate to ScheduleConfig to parse the JSON body
    std::string bodyStr(body->buf, body->len);
    ScheduleConfig config;
    // We can't parse from string directly, so we use a temp approach:
    // Extract ranges array manually
    std::vector<ScheduleRange> ranges;
    size_t pos = bodyStr.find("\"ranges\"");
    if (pos != std::string::npos) {
        pos = bodyStr.find('[', pos);
        if (pos != std::string::npos) {
            int depth = 0;
            size_t endPos = pos;
            for (size_t i = pos; i < bodyStr.size(); ++i) {
                if (bodyStr[i] == '[') depth++;
                else if (bodyStr[i] == ']') {
                    depth--;
                    if (depth == 0) {
                        endPos = i;
                        break;
                    }
                }
            }
            std::string arrayContent = bodyStr.substr(pos, endPos - pos + 1);

            // Parse each object
            size_t objStart = 0;
            while ((objStart = arrayContent.find('{', objStart)) != std::string::npos) {
                size_t objEnd = arrayContent.find('}', objStart);
                if (objEnd == std::string::npos) break;

                ScheduleRange range;
                std::string obj = arrayContent.substr(objStart, objEnd - objStart + 1);

                // Helper lambda to extract a field
                auto extractInt = [&](const std::string& key) -> std::optional<int> {
                    std::string k = "\"" + key + "\"";
                    size_t kp = obj.find(k);
                    if (kp == std::string::npos) return std::nullopt;
                    size_t cp = obj.find(':', kp + k.size());
                    if (cp == std::string::npos) return std::nullopt;
                    std::string val = obj.substr(cp + 1);
                    // Trim whitespace and comma
                    auto fs = val.find_first_not_of(" \t");
                    if (fs == std::string::npos) return std::nullopt;
                    val = val.substr(fs);
                    auto ce = val.find_first_of(",}");
                    if (ce != std::string::npos) val = val.substr(0, ce);
                    try { return std::stoi(val); } catch (...) { return std::nullopt; }
                };

                auto extractFloat = [&](const std::string& key) -> std::optional<float> {
                    std::string k = "\"" + key + "\"";
                    size_t kp = obj.find(k);
                    if (kp == std::string::npos) return std::nullopt;
                    size_t cp = obj.find(':', kp + k.size());
                    if (cp == std::string::npos) return std::nullopt;
                    std::string val = obj.substr(cp + 1);
                    auto fs = val.find_first_not_of(" \t");
                    if (fs == std::string::npos) return std::nullopt;
                    val = val.substr(fs);
                    auto ce = val.find_first_of(",}");
                    if (ce != std::string::npos) val = val.substr(0, ce);
                    try { return std::stof(val); } catch (...) { return std::nullopt; }
                };

                auto s = extractInt("start");
                auto e = extractInt("end");
                auto m = extractInt("mode");
                auto ht = extractFloat("heatingTemp");
                auto dt = extractFloat("dhwTemp");

                if (s && e && m) {
                    range.startMinutes = *s;
                    range.endMinutes = *e;
                    range.mode = *m;
                    if (ht) range.heatingTemp = *ht;
                    if (dt) range.dhwTemp = *dt;
                    ranges.push_back(range);
                }

                objStart = objEnd + 1;
            }
        }
    }

    if (!scheduler_->setRanges(ranges)) {
        sendJsonReply(c, 422, "{\"error\":\"Invalid schedule ranges\"}");
        return;
    }

    sendJsonReply(c, 200, "{\"success\":true}");
}

void WebServer::apiScheduleToggleHandler(struct mg_connection* c, struct mg_str* body) {
    if (!scheduler_) {
        sendJsonReply(c, 503, "{\"error\":\"Scheduler not available\"}");
        return;
    }

    // Accepts both JSON bool and JSON number
    bool enabledVal = false;
    if (mg_json_get_bool(*body, "$.enabled", &enabledVal) >= 1) {
        scheduler_->setEnabled(enabledVal);
        sendJsonReply(c, 200, "{\"success\":true}");
    } else {
        double numVal = 0;
        if (mg_json_get_num(*body, "$.enabled", &numVal) >= 1) {
            scheduler_->setEnabled(numVal != 0);
            sendJsonReply(c, 200, "{\"success\":true}");
        } else {
            sendJsonReply(c, 400, "{\"error\":\"Missing enabled parameter\"}");
        }
    }
}

void WebServer::apiScheduleResumeHandler(struct mg_connection* c) {
    if (!scheduler_) {
        sendJsonReply(c, 503, "{\"error\":\"Scheduler not available\"}");
        return;
    }

    scheduler_->resume();
    sendJsonReply(c, 200, "{\"success\":true}");
}

} // namespace windmi