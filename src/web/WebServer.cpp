/**
 * @file src/web/WebServer.cpp
 * @brief Web server C++ implementation
 */

#include "web/WebServer.hpp"
#include "utils/JsonHelpers.hpp"
#include "config.h"

#include <mongoose.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace windmi {

struct WebServer::Impl {
    struct mg_mgr mgr;
    struct mg_connection* conn;
    bool running;
    volatile sig_atomic_t shutting_down;
    std::function<void()> status_callback;
    
    Impl() : running{false}, shutting_down{0} {
        mg_mgr_init(&mgr);
    }
    
    ~Impl() {
        mg_mgr_free(&mgr);
    }
};

WebServer::WebServer(int port, const std::string& static_dir) {
    (void)static_dir;  // TODO: Use static_dir
    impl_ = std::make_unique<Impl>();
    
    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d", WEB_SERVER_IP, port);
    
    impl_->conn = mg_http_listen(&impl_->mgr, url, nullptr, nullptr);
    if (!impl_->conn) {
        throw std::runtime_error("Failed to start web server");
    }
    
    printf("Web server started on %s\n", url);
}

WebServer::~WebServer() {
    if (impl_) {
        mg_mgr_free(&impl_->mgr);
    }
}

bool WebServer::start() {
    impl_->running = true;
    return true;
}

void WebServer::stop() {
    if (!impl_->running) return;
    
    impl_->shutting_down = 1;
    impl_->running = false;
    mg_mgr_free(&impl_->mgr);
}

bool WebServer::isRunning() const {
    return impl_->running;
}

bool WebServer::isShuttingDown() const {
    return impl_->shutting_down != 0;
}

void WebServer::setStatusCallback(std::function<void()> callback) {
    impl_->status_callback = callback;
}

} // namespace windmi
