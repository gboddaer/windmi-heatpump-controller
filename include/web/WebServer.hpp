/**
 * @file web/WebServer.hpp
 * @brief Web server for heat pump control interface
 */

#ifndef WINDMI_WEB_WEB_SERVER_HPP
#define WINDMI_WEB_WEB_SERVER_HPP

#include <string>
#include <functional>
#include <memory>

namespace windmi {

/**
 * @brief Web server class
 * 
 * Manages the HTTP server and API endpoints.
 */
class WebServer {
public:
    /**
     * @brief Constructor
     * @param port Port to listen on
     * @param static_dir Directory for static files
     */
    WebServer(int port, const std::string& static_dir);

    /**
     * @brief Destructor
     */
    ~WebServer();

    /**
     * @brief Start the web server
     * @return true if successful, false otherwise
     */
    bool start();

    /**
     * @brief Stop the web server
     */
    void stop();

    /**
     * @brief Check if server is running
     * @return true if running, false otherwise
     */
    bool isRunning() const;

    /**
     * @brief Check if server is shutting down
     * @return true if shutting down, false otherwise
     */
    bool isShuttingDown() const;

    /**
     * @brief Set status callback for API endpoints
     * @param callback Callback function
     */
    void setStatusCallback(std::function<void()> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace windmi

#endif // WINDMI_WEB_WEB_SERVER_HPP
