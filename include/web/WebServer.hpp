/**
 * @file web/WebServer.hpp
 * @brief Web server for heat pump control interface
 *
 * Matches master branch web_server.h / web_server.c functionality.
 */

#ifndef WINDMI_WEB_WEB_SERVER_HPP_
#define WINDMI_WEB_WEB_SERVER_HPP_

#include "core/ControlLoop.hpp"
#include <mongoose.h>
#undef poll // Mongoose defines poll(a,b,c) as WSAPoll on Windows; conflicts with our method name
#include <string>
#include <atomic>
#include <csignal>
#include <functional>

namespace windmi {

/**
 * @brief Web server class
 *
 * Manages the HTTP server, API endpoints, and static file serving.
 * Uses Mongoose embedded web server.
 * Matches master branch web_server.c functionality.
 */
class WebServer
{
public:
  /**
   * @brief Constructor
   * @param port Port to listen on
   * @param static_dir Directory for static files
   * @param cmd_queue Command queue for pushing API commands
   * @param status_queue Status queue for reading status snapshots
   */
  WebServer(int port, const std::string& static_dir, CmdQueue* cmd_queue, StatusQueue* status_queue,
            std::function<void()> wake_callback = nullptr);

  ~WebServer();

  /**
   * @brief Run the web server (blocking)
   *
   * This call blocks until stop() is called. It runs the
   * mg_mgr_poll() event loop.
   */
  void run();

  /**
   * @brief Poll the web server once.
   * @param timeout_ms Poll timeout in milliseconds.
   */
  void pollOnce(int timeout_ms = 100);

  void stop();
  bool isShuttingDown() const;

private:
  static void eventHandler(struct mg_connection* c, int ev, void* ev_data);
  void handleRequest(struct mg_connection* c, struct mg_http_message* hm);

  static void sendJsonReply(struct mg_connection* c, int status_code, const char* json);

  void apiStatusHandler(struct mg_connection* c);
  void apiSetDhwHandler(struct mg_connection* c, struct mg_str* body);
  void apiSetHeatingHandler(struct mg_connection* c, struct mg_str* body);
  void apiSetPriorityHandler(struct mg_connection* c, struct mg_str* body);
  void apiSetModeHandler(struct mg_connection* c, struct mg_str* body);

  struct mg_mgr mMgr;
  std::string mStaticDir;
  CmdQueue* mCmdQueue;
  StatusQueue* mStatusQueue;
  StatusSnapshot mLastStatus;
  std::atomic<bool> mRunning;
  volatile sig_atomic_t mShuttingDown;
  bool mMgrFreed;
  std::function<void()> mWakeCallback;
};

} // namespace windmi

#endif // WINDMI_WEB_WEB_SERVER_HPP