/**
 * @file src/web/WebServer.cpp
 * @brief Web server C++ implementation matching master branch web_server.c
 */

#include "web/WebServer.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include "config.h"

#include <mongoose.h>
#undef poll // Mongoose defines poll(a,b,c) as WSAPoll on Windows
#include <cstdio>

#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace windmi {

// ---- Static helper functions (matching master) ----

static const char* mode_to_string(int mode)
{
  switch (mode)
  {
  case MODE_SET_OFF:
    return "off";
  case MODE_SET_COOL_DHW:
    return "cool+dhw";
  case MODE_SET_HEAT_DHW:
    return "heat+dhw";
  default:
    return "unknown";
  }
}

static const char* status_to_string(int status)
{
  switch (status)
  {
  case MODE_STATUS_OFF:
    return "off";
  case MODE_STATUS_COOL:
    return "cool";
  case MODE_STATUS_HEAT:
    return "heat";
  case MODE_STATUS_DHW:
    return "dhw";
  case MODE_STATUS_DEFROST:
    return "defrost";
  case MODE_STATUS_ANTIFREEZE:
    return "antifreeze";
  default:
    return "unknown";
  }
}

// ---- WebServer implementation ----

WebServer::WebServer(int port, const std::string& static_dir, CmdQueue* cmd_queue,
                     StatusQueue* status_queue, std::function<void()> wake_callback)
    : mStaticDir(static_dir), mCmdQueue(cmd_queue), mStatusQueue(status_queue), mLastStatus{},
      mRunning(false), mShuttingDown(0), mMgrFreed(false),
      mWakeCallback(std::move(wake_callback))
{
  mg_mgr_init(&mMgr);

  char url[64];
  snprintf(url, sizeof(url), "http://%s:%d", WEB_SERVER_IP, port);

  struct mg_connection* conn = mg_http_listen(&mMgr, url, WebServer::eventHandler, this);
  if (!conn)
  {
    WINDMI_LOG_ERROR(LOG_TAG_WEBSERVER, "Failed to start server on %s", url);
    mg_mgr_free(&mMgr);
    mMgrFreed = true;
    throw std::runtime_error("Failed to start web server");
  }

  WINDMI_LOG_INFO(LOG_TAG_WEBSERVER, "Started on %s", url);
  WINDMI_LOG_INFO(LOG_TAG_WEBSERVER, "Static files served from: %s", mStaticDir.c_str());

  mRunning.store(true);
}

WebServer::~WebServer()
{
  stop();
  if (!mMgrFreed)
  {
    mg_mgr_free(&mMgr);
    mMgrFreed = true;
  }
}

void WebServer::run()
{
  while (mRunning.load())
  {
    pollOnce(100);
  }
}

void WebServer::pollOnce(int timeout_ms)
{
  if (mRunning.load())
  {
    mg_mgr_poll(&mMgr, timeout_ms);
  }
}

void WebServer::stop()
{
  mShuttingDown = 1;
  mRunning.store(false);
}

bool WebServer::isShuttingDown() const
{
  return mShuttingDown != 0;
}

void WebServer::sendJsonReply(struct mg_connection* c, int status_code, const char* json)
{
  mg_http_reply(c, status_code,
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n",
                "%s", json);
}

// ---- Static event handler (mongoose callback) ----

void WebServer::eventHandler(struct mg_connection* c, int ev, void* ev_data)
{
  if (ev != MG_EV_HTTP_MSG)
    return;

  auto* self = static_cast<WebServer*>(c->fn_data);
  if (!self)
    return;

  auto* hm = static_cast<struct mg_http_message*>(ev_data);
  self->handleRequest(c, hm);
}

// ---- HTTP request routing ----

void WebServer::handleRequest(struct mg_connection* c, struct mg_http_message* hm)
{
  if (mg_strcmp(hm->uri, mg_str("/api/status")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("GET")) == 0)
    {
      apiStatusHandler(c);
    } else
    {
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
    }
  } else if (mg_strcmp(hm->uri, mg_str("/api/set-dhw")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiSetDhwHandler(c, &hm->body);
    } else
    {
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
    }
  } else if (mg_strcmp(hm->uri, mg_str("/api/set-heating")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiSetHeatingHandler(c, &hm->body);
    } else
    {
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
    }
  } else if (mg_strcmp(hm->uri, mg_str("/api/set-priority")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiSetPriorityHandler(c, &hm->body);
    } else
    {
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
    }
  } else if (mg_strcmp(hm->uri, mg_str("/api/set-mode")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiSetModeHandler(c, &hm->body);
    } else
    {
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
    }
  } else
  {
    // Serve static files
    struct mg_http_serve_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.root_dir = mStaticDir.c_str();
    mg_http_serve_dir(c, hm, &opts);
  }
}

// ---- API handlers (matching master branch web_server.c) ----

void WebServer::apiStatusHandler(struct mg_connection* c)
{
  StatusSnapshot snapshot;
  if (mStatusQueue && mStatusQueue->latest(snapshot))
  {
    mLastStatus = snapshot;
  }

  char response[4096];
  snprintf(
      response, sizeof(response),
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
      mLastStatus.dhw_tank_temp, mLastStatus.dhw_target, mLastStatus.leaving_water_temp,
      mLastStatus.heating_target, mLastStatus.outdoor_temp, mLastStatus.leaving_water_temp,
      mLastStatus.entering_water_temp, mode_to_string(mLastStatus.running_mode),
      status_to_string(mLastStatus.running_status), mLastStatus.dhw_priority ? "dhw" : "heating",
      mLastStatus.is_running ? "running" : "stopped",
      mLastStatus.device_online ? "true" : "false", mLastStatus.ac_current,
      mLastStatus.dc_current, mLastStatus.ac_voltage, mLastStatus.dc_voltage,
      mLastStatus.ac_power_va, mLastStatus.ac_power_w,
      mLastStatus.power_valid ? "true" : "false", mLastStatus.compressor_freq,
      mLastStatus.water_flow, mLastStatus.unit_capacity_kw, mLastStatus.actual_capacity_output,
      mLastStatus.odu_input_status, mLastStatus.compressor_runtime_h, mLastStatus.pump_runtime_h,
      mLastStatus.heat_output_w, mLastStatus.cop, mLastStatus.cop_valid ? "true" : "false",
      mLastStatus.working_mode);

  sendJsonReply(c, 200, response);
}

void WebServer::apiSetDhwHandler(struct mg_connection* c, struct mg_str* body)
{
  if (isShuttingDown())
  {
    sendJsonReply(c, 503, "{\"error\":\"Server is shutting down\"}");
    return;
  }

  if (body->len == 0)
  {
    sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
    return;
  }

  double temperature = 0;
  if (mg_json_get_num(*body, "$.temperature", &temperature) < 1)
  {
    sendJsonReply(c, 400, "{\"error\":\"Missing or invalid temperature\"}");
    return;
  }

  if (temperature < DHW_TEMP_MIN || temperature > DHW_TEMP_MAX)
  {
    sendJsonReply(c, 422, "{\"error\":\"Temperature out of range\"}");
    return;
  }

  WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER,
                   "Received DHW set request - temperature=%.1f C (range: %.0f-%.0f)", temperature,
                   static_cast<double>(DHW_TEMP_MIN), static_cast<double>(DHW_TEMP_MAX));

  Command cmd;
  cmd.type = CommandType::CMD_SET_DHW_TEMP;
  cmd.float_val = static_cast<float>(temperature);
  cmd.int_val = 0;
  bool pushed = mCmdQueue ? mCmdQueue->push(cmd) : false;
  if (pushed && mWakeCallback)
    mWakeCallback();
  WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "DHW command pushed to queue (temp=%.1f, pushed=%d)",
                   temperature, pushed);
  sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

void WebServer::apiSetHeatingHandler(struct mg_connection* c, struct mg_str* body)
{
  if (isShuttingDown())
  {
    sendJsonReply(c, 503, "{\"error\":\"Server is shutting down\"}");
    return;
  }

  if (body->len == 0)
  {
    sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
    return;
  }

  double temperature = 0;
  if (mg_json_get_num(*body, "$.temperature", &temperature) < 1)
  {
    sendJsonReply(c, 400, "{\"error\":\"Missing or invalid temperature\"}");
    return;
  }

  if (temperature < HEATING_TEMP_MIN || temperature > HEATING_TEMP_MAX)
  {
    sendJsonReply(c, 422, "{\"error\":\"Temperature out of range\"}");
    return;
  }

  WINDMI_LOG_DEBUG(
      LOG_TAG_WEBSERVER, "Received heating set request - temperature=%.1f C (range: %.0f-%.0f)",
      temperature, static_cast<double>(HEATING_TEMP_MIN), static_cast<double>(HEATING_TEMP_MAX));

  Command cmd;
  cmd.type = CommandType::CMD_SET_HEATING_TEMP;
  cmd.float_val = static_cast<float>(temperature);
  cmd.int_val = 0;
  bool pushed = mCmdQueue ? mCmdQueue->push(cmd) : false;
  if (pushed && mWakeCallback)
    mWakeCallback();
  WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Heating command pushed to queue (temp=%.1f, pushed=%d)",
                   temperature, pushed);
  sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

void WebServer::apiSetPriorityHandler(struct mg_connection* c, struct mg_str* body)
{
  if (isShuttingDown())
  {
    sendJsonReply(c, 503, "{\"error\":\"Server is shutting down\"}");
    return;
  }

  if (body->len == 0)
  {
    sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
    return;
  }

  char* priority = mg_json_get_str(*body, "$.priority");
  if (priority == nullptr)
  {
    sendJsonReply(c, 400, "{\"error\":\"Missing or invalid priority\"}");
    return;
  }

  int pri_val;
  if (strncmp(priority, "dhw", 3) == 0)
  {
    pri_val = 1;
  } else if (strncmp(priority, "heating", 7) == 0)
  {
    pri_val = 0;
  } else
  {
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
  bool pushed = mCmdQueue ? mCmdQueue->push(cmd) : false;
  if (pushed && mWakeCallback)
    mWakeCallback();
  WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Priority command pushed to queue (pri_val=%d, pushed=%d)",
                   pri_val, pushed);
  sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

void WebServer::apiSetModeHandler(struct mg_connection* c, struct mg_str* body)
{
  if (isShuttingDown())
  {
    sendJsonReply(c, 503, "{\"error\":\"Server is shutting down\"}");
    return;
  }

  if (body->len == 0)
  {
    sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
    return;
  }

  // Parse mode (0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating)
  long mode = mg_json_get_long(*body, "$.mode", -1);
  if (mode == -1)
  {
    sendJsonReply(c, 400, "{\"error\":\"Missing mode parameter\"}");
    return;
  }

  // Note: master also parses optional priority field (defaults to DHW=1)
  // but doesn't use it separately—it's handled by control_loop
  long priority = mg_json_get_long(*body, "$.priority", 1);

  WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Received mode set request - mode=%ld, priority=%ld", mode,
                   priority);

  Command mode_cmd;
  mode_cmd.type = CommandType::CMD_SET_RUNNING_MODE;
  mode_cmd.float_val = 0.0f;
  mode_cmd.int_val = static_cast<int>(mode);

  bool pushed = mCmdQueue ? mCmdQueue->push(mode_cmd) : false;
  if (pushed && mWakeCallback)
    mWakeCallback();
  WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Mode command pushed to queue (mode=%ld, pushed=%d)", mode,
                   pushed);

  sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

} // namespace windmi