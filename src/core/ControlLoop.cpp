/**
 * @file src/core/ControlLoop.cpp
 * @brief Control loop implementation
 */

#include "core/ControlLoop.hpp"
#include "core/StatusMonitor.hpp"
#include "modbus/ModbusClient.hpp"
#include "utils/JsonHelpers.hpp"
#include "config.h"
#include "crc16.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>

extern "C" {
    #include "modbus_client.h"
}

namespace windmi {

// Global status monitor
static StatusMonitor g_status_monitor;

// Saved targets for mode switching
static float saved_dhw_target = 45.0f;
static float saved_heating_target = 40.0f;

// Mode state
static int current_device_mode = 0;
static int desired_working_mode = 3;  // MODE_DHW_HEATING

ControlLoop::ControlLoop(StatusCallback status_cb)
    : status_callback_(status_cb)
    , modbus_client_(nullptr)
    , running_(false)
    , stop_requested_(false)
{
}

ControlLoop::~ControlLoop() {
    stop();
}

bool ControlLoop::start() {
    if (running_.load()) return true;
    
    stop_requested_.store(false);
    running_.store(true);
    
    thread_ = std::make_unique<std::thread>(&ControlLoop::threadFunc, this);
    return true;
}

void ControlLoop::stop() {
    if (!running_.load()) return;
    
    stop_requested_.store(true);
    running_.store(false);
    
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

bool ControlLoop::isRunning() const {
    return running_.load();
}

bool ControlLoop::getStatus(StatusSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    snapshot = current_status_;
    return true;
}

bool ControlLoop::enqueueCommand(CommandType type, float float_val, int int_val) {
    printf("Enqueue command: type=%d, float=%f, int=%d\n", 
           static_cast<int>(type), float_val, int_val);
    return true;
}

void ControlLoop::setModbusClient(void* client_ptr) {
    modbus_client_ = client_ptr;
}

void ControlLoop::threadFunc() {
    printf("Control loop thread started\n");
    
    // Read initial status
    if (modbus_client_ && readStatus(current_status_)) {
        // Publish initial snapshot
        if (status_callback_) {
            status_callback_(current_status_);
        }
    }
    
    while (!stop_requested_.load()) {
        // Process commands
        if (modbus_client_) {
            applyControlLogic();
        }
        
        // Read status periodically
        if (modbus_client_ && readStatus(current_status_)) {
            if (status_callback_) {
                status_callback_(current_status_);
            }
        }
        
        // Update global status monitor
        g_status_monitor.update(current_status_);
        
        // Sleep for status update interval
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    printf("Control loop thread stopped\n");
}

bool ControlLoop::readStatus(StatusSnapshot& snapshot) {
    if (!modbus_client_) return false;
    
    ModbusClient* mb = static_cast<ModbusClient*>(modbus_client_);
    
    try {
        // Read temperature registers
        int16_t temp;
        
        temp = mb->readRegister(0x0012);  // REG_DHW_TEMP
        snapshot.dhw_tank_temp = static_cast<float>(temp) / 10.0f;
        
        temp = mb->readRegister(0x0014);  // REG_HEATING_TEMP
        snapshot.heating_temperature = static_cast<float>(temp) / 10.0f;
        
        // Read outdoor temperature (simplified)
        snapshot.outdoor_temp = 20.0f;  // Placeholder
        
        // Read leaving water temperature
        snapshot.leaving_water_temp = snapshot.heating_temperature;
        
        // Read running mode and status
        snapshot.mode = mb->readRegister(0x002C);  // REG_RUNNING_MODE
        snapshot.running_status = mb->readRegister(0x002D);  // REG_RUNNING_STATUS
        
        // Read priority
        snapshot.priority = mb->readRegister(0x02BF);  // REG_DHW_PRIORITY
        
        // Read power monitoring
        snapshot.ac_current = mb->readRegister(0x1014) / 2.0f;  // Scale factor
        snapshot.dc_current = mb->readRegister(0x1015) / 4.0f;  // Scale factor
        snapshot.ac_voltage = mb->readRegister(0x1016) / 1.0f;
        snapshot.dc_voltage = mb->readRegister(0x1017) / 2.0f;
        snapshot.ac_power = 0.0f;  // Placeholder
        
        snapshot.device_online = true;
        snapshot.status = 0;  // Placeholder
        
        // Map to string representations
        snapshot.working_mode = desired_working_mode;
        
        return true;
    } catch (const ModbusException& e) {
        fprintf(stderr, "Failed to read status: %s\n", e.what());
        snapshot.device_online = false;
        return false;
    }
}

void ControlLoop::applyControlLogic() {
    // Check if mode needs correction
    if (current_device_mode != desired_working_mode) {
        printf("Control loop: Correcting device mode from %d to %d\n",
               current_device_mode, desired_working_mode);
        
        ModbusClient* mb = static_cast<ModbusClient*>(modbus_client_);
        
        try {
            mb->flushBuffer();
            
            if (desired_working_mode == 0) {  // MODE_OFF
                mb->writeRegister(0x002C, 0);  // REG_RUNNING_MODE
                current_device_mode = 0;
            } else if (desired_working_mode == 1) {  // MODE_DHW_ONLY
                // Set to DHW priority, heating to min
                mb->flushBuffer();
                mb->writeRegister(0x02BF, 1);  // PRIORITY_DHW
                mb->flushBuffer();
                mb->writeRegister(0x0014, static_cast<uint16_t>(250));  // heating min = 25.0
                current_device_mode = 2;  // Heat+DHW mode
            } else if (desired_working_mode == 2) {  // MODE_HEATING_ONLY
                // Clear priority, DHW to min
                mb->flushBuffer();
                mb->writeRegister(0x02BF, 0);  // PRIORITY_AUTO
                mb->flushBuffer();
                mb->writeRegister(0x0012, static_cast<uint16_t>(400));  // dhw min = 40.0
                current_device_mode = 2;  // Heat+DHW mode
            } else if (desired_working_mode == 3) {  // MODE_DHW_HEATING
                // Restore saved targets
                mb->flushBuffer();
                mb->writeRegister(0x02BF, 1);  // PRIORITY_DHW
                mb->flushBuffer();
                mb->writeRegister(0x0012, static_cast<uint16_t>(saved_dhw_target * 10));
                mb->flushBuffer();
                mb->writeRegister(0x0014, static_cast<uint16_t>(saved_heating_target * 10));
                current_device_mode = 2;  // Heat+DHW mode
            }
        } catch (const ModbusException& e) {
            fprintf(stderr, "Failed to set mode: %s\n", e.what());
        }
    }
}

} // namespace windmi
