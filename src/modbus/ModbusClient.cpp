/**
 * @file src/modbus/ModbusClient.cpp
 * @brief Modbus client C++ wrapper implementation
 */

#include "modbus/ModbusClient.hpp"
#include "config.h"

#include <cstdio>

extern "C" {
    #include "modbus_client.h"
}

namespace windmi {

struct ModbusClient::Impl {
    modbus_client_t* client;
    
    Impl() : client{nullptr} {}
    
    ~Impl() {
        disconnect();
    }
    
    void disconnect() {
        if (client) {
            modbus_client_disconnect(client);
            modbus_client_destroy(client);
            client = nullptr;
        }
    }
};

ModbusClient::ModbusClient(const std::string& host, int port, uint8_t slave_id) {
    impl_ = std::make_unique<Impl>();
    
    impl_->client = modbus_client_create(host.c_str(), port, slave_id);
    if (!impl_->client) {
        throw ModbusException("Failed to create Modbus client");
    }
}

ModbusClient::~ModbusClient() {
    disconnect();
}

bool ModbusClient::connect() {
    if (!impl_ || !impl_->client) return false;
    return modbus_client_connect(impl_->client);
}

void ModbusClient::disconnect() {
    if (impl_) {
        impl_->disconnect();
    }
}

bool ModbusClient::isConnected() const {
    if (!impl_ || !impl_->client) return false;
    return modbus_client_is_connected(impl_->client);
}

int16_t ModbusClient::readRegister(uint16_t address) {
    if (!impl_ || !impl_->client) {
        throw ModbusException("Not connected");
    }
    
    int16_t value;
    if (modbus_read_register(impl_->client, address, &value) != 0) {
        throw ModbusException("Failed to read register");
    }
    return value;
}

void ModbusClient::writeRegister(uint16_t address, uint16_t value) {
    if (!impl_ || !impl_->client) {
        throw ModbusException("Not connected");
    }
    
    // modbus_write_register returns 0 on success, -1 on error
    if (modbus_write_register(impl_->client, address, value) != 0) {
        throw ModbusException("Failed to write register");
    }
}

void ModbusClient::flushBuffer() {
    if (impl_ && impl_->client) {
        modbus_client_flush_buffer(impl_->client);
    }
}

std::string ModbusClient::getLastError() const {
    return "No error";
}

void* ModbusClient::getCClient() const {
    return impl_ ? impl_->client : nullptr;
}

} // namespace windmi
