/**
 * @file src/web/WebServer.cpp
 * @brief Web server C++ implementation matching master branch web_server.c
 */

#include "web/WebServer.hpp"
#include "web/DiagnosticsJson.hpp"
#include "web/BackupHeaterJson.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include "utils/ScheduleConfig.hpp"
#include "core/ModeNames.hpp"
#include <cinttypes>

#include "config.h"

#include <mongoose.h>
#undef poll // Mongoose defines poll(a,b,c) as WSAPoll on Windows
#include <cstdio>

#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <sstream>

namespace windmi {

// ---- Static helper functions (matching master) ----

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
                     StatusQueue* status_queue, Scheduler* scheduler,
                     std::function<void()> wake_callback)
    : mStaticDir(static_dir), mCmdQueue(cmd_queue), mStatusQueue(status_queue),
      mScheduler(scheduler), mLastStatus{}, mRunning(false), mShuttingDown(0), mMgrFreed(false),
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
  } else if (mg_strcmp(hm->uri, mg_str("/api/set-occupancy")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiSetOccupancyHandler(c, &hm->body);
    } else
    {
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
    }
  } else if (mg_strcmp(hm->uri, mg_str("/api/price")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiPriceHandler(c, &hm->body);
    } else
    {
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
    }
  } else if (mg_strcmp(hm->uri, mg_str("/api/schedule")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("GET")) == 0)
    {
      apiScheduleGetHandler(c);
    } else if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiScheduleSetHandler(c, &hm->body);
    } else
    {
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
    }
  } else if (mg_strcmp(hm->uri, mg_str("/api/schedule/defaults")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiScheduleDefaultsHandler(c, &hm->body);
    } else
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
  } else if (mg_strcmp(hm->uri, mg_str("/api/schedule/toggle")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiScheduleToggleHandler(c, &hm->body);
    } else
    {
      sendJsonReply(c, 405, "{\"error\":\"Method not allowed\"}");
    }
  } else if (mg_strcmp(hm->uri, mg_str("/api/schedule/resume")) == 0)
  {
    if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
    {
      apiScheduleResumeHandler(c);
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

  char heating_cop[48] = "null", dhw_cop[48] = "null";
  if (mLastStatus.heating_cycle_cop_valid)
    snprintf(heating_cop, sizeof(heating_cop), "%.2f", mLastStatus.heating_cycle_cop);
  if (mLastStatus.dhw_cycle_cop_valid)
    snprintf(dhw_cop, sizeof(dhw_cop), "%.2f", mLastStatus.dhw_cycle_cop);
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
      "\"dailyEnergyKwh\":%.3f,"
      "\"dailyEnergyValid\":%s,"
      "\"pricePerKwhCents\":%.2f,"
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
      "\"heatOutputValid\":%s,"
      "\"heatingCycleCop\":%s,"
      "\"dhwCycleCop\":%s,"
      "\"powerCalibration\":\"hypothesis: AC current = raw / 2; PF = 0.90\","

      "\"dhwValveStatus\":%d,"
      "\"dhwModeStatus\":%d,"
      "\"workingMode\":%d,"
      "\"occupancyMode\":%d,"
      "\"scheduleActive\":%s,"
      "\"schedulePaused\":%s,"
      "\"scheduleEnabled\":%s,"
      "\"schedulePending\":%s,"
      "\"schedulePersistenceError\":%s,"
      "\"scheduleRevision\":%" PRIu64 ","
      "\"scheduleInstanceId\":\"%s\","
      "\"controlSource\":\"%s\","
      "\"scheduleDefaults\":{\"mode\":%d,\"heatingTemp\":%.1f,\"dhwTemp\":%.1f},"
      "\"dhwCalling\":%s,"
      "\"dhwStateKnown\":%s,"
      "\"requestedDeviceMode\":\"%s\","
      "\"secondsSinceModeWrite\":%d,"
      "\"dhwSensorFaultSuspected\":%s,"
      "\"tankReadFailures\":%d,"
      "\"dhwState\":\"%s\","
      "\"unitActuating\":%s,"
      "\"dhwTarget\":%.1f,"
      "\"dhwEcoTarget\":%.1f,"
      "\"effectiveDhwTarget\":%.1f,"
      "\"waterControlPoint\":%.1f,"
      "\"statusSequence\":%" PRIu64 "}",
      mLastStatus.dhw_tank_temp, mLastStatus.dhw_target, mLastStatus.leaving_water_temp,
      mLastStatus.heating_target, mLastStatus.outdoor_temp, mLastStatus.leaving_water_temp,
      mLastStatus.entering_water_temp, mode_to_string(mLastStatus.running_mode),
      status_to_string(mLastStatus.running_status), mLastStatus.dhw_priority ? "dhw" : "heating",
      mLastStatus.is_running ? "running" : "stopped", mLastStatus.device_online ? "true" : "false",
      mLastStatus.ac_current, mLastStatus.dc_current, mLastStatus.ac_voltage,
      mLastStatus.dc_voltage, mLastStatus.ac_power_va, mLastStatus.ac_power_w,
      mLastStatus.power_valid ? "true" : "false", mLastStatus.daily_energy_kwh,
      mLastStatus.daily_energy_valid ? "true" : "false", mLastStatus.price_cents_per_kwh,
      mLastStatus.compressor_freq,
      mLastStatus.water_flow, mLastStatus.unit_capacity_kw, mLastStatus.actual_capacity_output,
      mLastStatus.odu_input_status, mLastStatus.compressor_runtime_h, mLastStatus.pump_runtime_h,
      mLastStatus.heat_output_w, mLastStatus.cop, mLastStatus.cop_valid ? "true" : "false",
      mLastStatus.heat_output_valid ? "true" : "false", heating_cop, dhw_cop,
      mLastStatus.dhw_valve_status, mLastStatus.dhw_mode_status, mLastStatus.working_mode,
      mLastStatus.occupancy_mode, mLastStatus.schedule_active ? "true" : "false",
      mLastStatus.schedule_paused ? "true" : "false",
      mLastStatus.schedule_enabled ? "true" : "false",
      mLastStatus.schedule_pending ? "true" : "false",
      mLastStatus.schedule_persistence_error ? "true" : "false",
      static_cast<uint64_t>(mLastStatus.schedule_revision),
      mLastStatus.schedule_instance_id.c_str(), mLastStatus.control_source.c_str(),
      mLastStatus.schedule_defaults.mode, mLastStatus.schedule_defaults.heatingTemp,
      mLastStatus.schedule_defaults.dhwTemp, mLastStatus.dhw_calling ? "true" : "false",
      mLastStatus.dhw_state_known ? "true" : "false",
      mode_to_string(mLastStatus.requested_device_mode), mLastStatus.seconds_since_mode_write,
      mLastStatus.dhw_sensor_fault_suspected ? "true" : "false", mLastStatus.tank_read_failures,
      mLastStatus.dhw_state.c_str(), mLastStatus.unit_actuating ? "true" : "false",
      mLastStatus.dhw_target, mLastStatus.dhw_eco_target, mLastStatus.effective_dhw_target,
      mLastStatus.water_control_point, static_cast<uint64_t>(mLastStatus.status_sequence));

  // Diagnostics can exceed the legacy fixed buffer. Append using a dynamic
  // string so a full register set cannot truncate the production JSON response.
  std::string payload(response);
  payload.pop_back();
  payload += ",\"diagnostics\":" + diagnosticsJson(mLastStatus.diagnostics);
  payload +=
      ",\"backupHeater\":" + backupHeaterJson(mLastStatus.diagnostics, mLastStatus.device_online) +
      '}';
  sendJsonReply(c, 200, payload.c_str());
}

void WebServer::apiPriceHandler(struct mg_connection* c, struct mg_str* body)
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

  double cents = 0;
  if (mg_json_get_num(*body, "$.priceCentsPerKwh", &cents) < 1)
  {
    sendJsonReply(c, 400, "{\"error\":\"Missing or invalid price\"}");
    return;
  }

  if (cents < static_cast<double>(PRICE_CENTS_MIN) || cents > static_cast<double>(PRICE_CENTS_MAX))
  {
    sendJsonReply(c, 422, "{\"error\":\"Price out of range\"}");
    return;
  }

  WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER,
                   "Received price set request - %.2f eurocents/kWh (range: %.0f-%.0f)", cents,
                   static_cast<double>(PRICE_CENTS_MIN), static_cast<double>(PRICE_CENTS_MAX));

  Command cmd;
  cmd.type = CommandType::CMD_SET_PRICE;
  cmd.float_val = static_cast<float>(cents);
  cmd.int_val = 0;
  bool pushed = mCmdQueue ? mCmdQueue->push(cmd) : false;
  if (pushed && mWakeCallback)
    mWakeCallback();
  // The save happens in the loop, so this reply promises a queue slot, not a file write.
  sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Price queued\"}");
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

void WebServer::apiSetOccupancyHandler(struct mg_connection* c, struct mg_str* body)
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

  // Parse occupancy mode (0=Away, 1=Sleep, 2=Home)
  long mode = mg_json_get_long(*body, "$.mode", -1);
  if (mode < 0 || mode > 2)
  {
    sendJsonReply(c, 422, "{\"error\":\"Invalid occupancy mode (0=Away, 1=Sleep, 2=Home)\"}");
    return;
  }

  WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Received occupancy set request - mode=%ld", mode);

  Command cmd;
  cmd.type = CommandType::CMD_SET_OCCUPANCY_MODE;
  cmd.float_val = 0.0f;
  cmd.int_val = static_cast<int>(mode);
  bool pushed = mCmdQueue ? mCmdQueue->push(cmd) : false;
  if (pushed && mWakeCallback)
    mWakeCallback();
  WINDMI_LOG_DEBUG(LOG_TAG_WEBSERVER, "Occupancy command pushed to queue (mode=%ld, pushed=%d)",
                   mode, pushed);
  sendJsonReply(c, 202, "{\"success\":true,\"verified\":false,\"message\":\"Command queued\"}");
}

// ---- Schedule API handlers ----

static std::string scheduleReply(const Scheduler& scheduler, bool success = false)
{
  auto json = ScheduleConfig::toJson(scheduler.snapshot(), true);
  return success ? "{\"success\":true," + json.substr(1) : json;
}

void WebServer::apiScheduleGetHandler(struct mg_connection* c)
{
  if (!mScheduler)
  {
    sendJsonReply(c, 503, "{\"error\":\"Scheduler not available\"}");
    return;
  }

  sendJsonReply(c, 200, scheduleReply(*mScheduler).c_str());
}

void WebServer::apiScheduleSetHandler(struct mg_connection* c, struct mg_str* body)
{
  if (!mScheduler)
  {
    sendJsonReply(c, 503, "{\"error\":\"Scheduler not available\"}");
    return;
  }

  if (body->len == 0)
  {
    sendJsonReply(c, 400, "{\"error\":\"Empty request body\"}");
    return;
  }

  ScheduleState candidate;
  std::string error;
  bool defaultsPresent = false;
  if (!ScheduleConfig::parseJson(std::string(body->buf, body->len), candidate, error,
                                 &defaultsPresent))
  {
    const std::string reply = "{\"error\":\"" + error + "\"}";
    sendJsonReply(c, 422, reply.c_str());
    return;
  }
  // Never accept paused=false from an editor as an implicit Resume.
  const bool saved =
      defaultsPresent ? mScheduler->replace(candidate.enabled, candidate.ranges, candidate.defaults)
                      : mScheduler->replace(candidate.enabled, candidate.ranges);
  if (!saved)
  {
    sendJsonReply(c, 500,
                  "{\"error\":\"Could not save schedule; previous configuration retained\"}");
    return;
  }
  if (mWakeCallback)
    mWakeCallback();
  sendJsonReply(c, 200, scheduleReply(*mScheduler, true).c_str());
}

void WebServer::apiScheduleDefaultsHandler(struct mg_connection* c, struct mg_str* body)
{
  if (!mScheduler)
  {
    sendJsonReply(c, 503, "{\"error\":\"Scheduler not available\"}");
    return;
  }
  ScheduleDefaults defaults;
  std::string error;
  if (!ScheduleConfig::parseDefaultsJson(std::string(body->buf, body->len), defaults, error))
  {
    sendJsonReply(c, 422, "{\"error\":\"Invalid or incomplete schedule defaults\"}");
    return;
  }
  if (!mScheduler->setDefaults(defaults))
  {
    sendJsonReply(c, 500,
                  "{\"error\":\"Could not save default; previous configuration retained\"}");
    return;
  }
  if (mWakeCallback)
    mWakeCallback();
  sendJsonReply(c, 200, scheduleReply(*mScheduler, true).c_str());
}
void WebServer::apiScheduleToggleHandler(struct mg_connection* c, struct mg_str* body)
{
  if (!mScheduler)
  {
    sendJsonReply(c, 503, "{\"error\":\"Scheduler not available\"}");
    return;
  }

  bool enabled = false;
  if (!ScheduleConfig::parseEnabledJson(std::string(body->buf, body->len), enabled))
  {
    sendJsonReply(c, 400, "{\"error\":\"Enabled must be boolean or 0/1\"}");
    return;
  }
  if (!mScheduler->setEnabled(enabled))
  {
    sendJsonReply(c, 500,
                  "{\"error\":\"Could not save schedule; previous configuration retained\"}");
    return;
  }
  if (mWakeCallback)
    mWakeCallback();
  sendJsonReply(c, 200, scheduleReply(*mScheduler, true).c_str());
}

void WebServer::apiScheduleResumeHandler(struct mg_connection* c)
{
  if (!mScheduler)
  {
    sendJsonReply(c, 503, "{\"error\":\"Scheduler not available\"}");
    return;
  }

  if (!mScheduler->resume())
  {
    sendJsonReply(c, 500, "{\"error\":\"Could not save schedule; pause retained\"}");
    return;
  }
  if (mWakeCallback)
    mWakeCallback();
  sendJsonReply(c, 200, scheduleReply(*mScheduler, true).c_str());
}

} // namespace windmi