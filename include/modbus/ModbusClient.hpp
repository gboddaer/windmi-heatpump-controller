/**
 * @file modbus/ModbusClient.hpp
 * @brief C++ wrapper for Modbus client
 */

#ifndef WINDMI_MODBUS_MODBUS_CLIENT_HPP
#define WINDMI_MODBUS_MODBUS_CLIENT_HPP

#include <cstdint>
#include <string>
#include <memory>
#include <stdexcept>

// Include the C Modbus client header
extern "C" {
    #include "modbus_client.h"
}

namespace windmi {

/**
 * @brief Modbus client exception class
 */
class ModbusException : public std::runtime_error {
public:
    explicit ModbusException(const std::string& msg)
        : std::runtime_error(msg) {}
};

/**
 * @brief Modbus client class
 * 
 * C++ wrapper around the C Modbus client implementation.
 * Provides RAII pattern and better error handling.
 */
class ModbusClient {
public:
    /**
     * @brief Constructor
     * @param host Hostname or IP address
     * @param port Port number
     * @param slave_id Modbus slave ID
     */
    ModbusClient(const std::string& host, int port, uint8_t slave_id);

    /**
     * @brief Destructor
     */
    ~ModbusClient();

    /**
     * @brief Copy constructor (deleted)
     */
    ModbusClient(const ModbusClient&) = delete;

    /**
     * @brief Assignment operator (deleted)
     */
    ModbusClient& operator=(const ModbusClient&) = delete;

    /**
     * @brief Connect to Modbus device
     * @return true if successful, false otherwise
     */
    bool connect();

    /**
     * @brief Disconnect from Modbus device
     */
    void disconnect();

    /**
     * @brief Check if connected
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Read a holding register
     * @param address Register address
     * @return Register value
     * @throws ModbusException on error
     */
    int16_t readRegister(uint16_t address);

    /**
     * @brief Write a holding register
     * @param address Register address
     * @param value Value to write
     * @throws ModbusException on error
     */
    void writeRegister(uint16_t address, uint16_t value);

    /**
     * @brief Flush read buffer
     * Clears any pending data in the socket read buffer.
     */
    void flushBuffer();

    /**
     * @brief Get last error message
     * @return Error message
     */
    std::string getLastError() const;

    /**
     * @brief Get underlying C client pointer
     * @return Pointer to C modbus_client structure
     */
    void* getCClient() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace windmi

#endif // WINDMI_MODBUS_MODBUS_CLIENT_HPP
