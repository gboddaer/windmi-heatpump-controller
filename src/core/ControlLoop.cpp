/**
 * @file src/core/ControlLoop.cpp
 * @brief Control loop implementation matching master branch control_loop.c
 */

#include "core/ControlLoop.hpp"
#include "modbus/IModbusClient.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include "config.h"

#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>

namespace windmi {

// ---- CmdQueue implementation ----

CmdQueue::CmdQueue() : head_(0), tail_(0) {}

bool CmdQueue::push(const Command& cmd) {
    const uint32_t current_tail = tail_.load(std::memory_order_relaxed);
    const uint32_t next_tail = (current_tail + 1) % CAPACITY;
    if (next_tail == head_.load(std::memory_order_acquire)) return false;  // Full
    buf_[current_tail] = cmd;
    tail_.store(next_tail, std::memory_order_release);
    return true;
}

bool CmdQueue::pop(Command& cmd) {
    const uint32_t current_head = head_.load(std::memory_order_relaxed);
    if (current_head == tail_.load(std::memory_order_acquire)) return false;  // Empty
    cmd = buf_[current_head];
    head_.store((current_head + 1) % CAPACITY, std::memory_order_release);
    return true;
}

bool CmdQueue::empty() const {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}

// ---- StatusQueue implementation ----

StatusQueue::StatusQueue() : head_(0), tail_(0) {}

bool StatusQueue::push(const StatusSnapshot& item) {
    const uint32_t current_tail = tail_.load(std::memory_order_relaxed);
    const uint32_t next_tail = (current_tail + 1) % CAPACITY;
    if (next_tail == head_.load(std::memory_order_acquire)) return false;  // Full
    buf_[current_tail] = item;
    tail_.store(next_tail, std::memory_order_release);
    return true;
}

bool StatusQueue::pop(StatusSnapshot& item) {
    const uint32_t current_head = head_.load(std::memory_order_relaxed);
    if (current_head == tail_.load(std::memory_order_acquire)) return false;  // Empty
    item = buf_[current_head];
    head_.store((current_head + 1) % CAPACITY, std::memory_order_release);
    return true;
}

bool StatusQueue::latest(StatusSnapshot& item) {
    bool found = false;
    StatusSnapshot tmp;
    while (pop(tmp)) {
        item = tmp;
        found = true;
    }
    return found;
}

// ---- Helper functions ----

static inline float raw_to_temp(int16_t raw) {
    return static_cast<float>(raw) / 10.0f;
}

static inline int16_t temp_to_raw(float temp) {
    return static_cast<int16_t>(temp * 10.0f);
}

// ---- ControlLoop implementation ----

ControlLoop::ControlLoop()
    : modbus_client_(nullptr)
    , cmd_queue_(nullptr)
    , status_queue_(nullptr)
    , running_(false)
    , stop_requested_(false)
    , kick_generation_(0)
    , current_priority_(PriorityMode::Dhw)
    , current_mode_(MODE_SET_HEAT_DHW)
    , desired_working_mode_(3)
    , last_heating_target_(45.0f)
    , saved_dhw_target_(46.0f)
    , saved_heating_target_(45.0f)
    , saved_targets_initialized_(false)
{
}

ControlLoop::~ControlLoop() {
    stop();
}

bool ControlLoop::start(IModbusClient* client, CmdQueue* cmd_queue, StatusQueue* status_queue) {
    if (running_.load()) return true;

    modbus_client_ = client;
    cmd_queue_ = cmd_queue;
    status_queue_ = status_queue;
    stop_requested_.store(false);
    running_.store(true);

    thread_ = std::make_unique<std::thread>(&ControlLoop::threadFunc, this);
    return true;
}

void ControlLoop::stop() {
    if (!running_.load()) return;

    stop_requested_.store(true);
    running_.store(false);

    // Wake the thread if it's sleeping
    kick();

    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

bool ControlLoop::isRunning() const {
    return running_.load();
}

void ControlLoop::kick() {
    std::lock_guard<std::mutex> lock(kick_mutex_);
    ++kick_generation_;
    kick_cond_.notify_one();
}

int ControlLoop::setRunningMode(int mode) {
    if (!modbus_client_) return -1;

    // Validate: only 0, 1, 2 are valid device modes
    if (mode < 0 || mode > 2) {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Invalid device mode %d: only 0 (Off), 1 (Cool+DHW), 2 (Heat+DHW) supported", mode);
        return -1;
    }

    try {
        modbus_client_->writeRegister(REG_RUNNING_MODE, static_cast<uint16_t>(mode));
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set running mode to %d", mode);
        return 0;
    } catch (const ModbusException& e) {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set running mode to %d: %s", mode, e.what());
        return -1;
    }
}

int ControlLoop::setDhwTarget(float temp) {
    if (!modbus_client_) return -1;

    int16_t raw_temp = temp_to_raw(temp);
    try {
        modbus_client_->writeRegister(REG_DHW_TARGET, static_cast<uint16_t>(raw_temp));
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set DHW target to %.1f C", temp);
        return 0;
    } catch (const ModbusException& e) {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set DHW target to %.1f: %s", temp, e.what());
        return -1;
    }
}

int ControlLoop::setHeatingTarget(float temp) {
    if (!modbus_client_) return -1;

    int16_t raw_temp = temp_to_raw(temp);
    try {
        modbus_client_->writeRegister(REG_HEATING_TARGET, static_cast<uint16_t>(raw_temp));
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set heating target to %.1f C", temp);
        return 0;
    } catch (const ModbusException& e) {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set heating target to %.1f: %s", temp, e.what());
        return -1;
    }
}

bool ControlLoop::readStatus(StatusSnapshot& status) {
    if (!modbus_client_) return false;

    bool ok = true;
    int16_t raw;

    // Read outdoor temp (0x0001)
    try {
        raw = modbus_client_->readRegister(REG_OUTDOOR_TEMP);
        status.outdoor_temp = raw_to_temp(raw);
    } catch (const ModbusException&) {
        ok = false;
    }

    // Read indoor temp (0x0002)
    try {
        raw = modbus_client_->readRegister(REG_INDOOR_TEMP);
        status.indoor_temp = raw_to_temp(raw);
    } catch (const ModbusException&) {
        // Non-critical, ignore
    }

    // Read leaving water temp (0x0004)
    try {
        raw = modbus_client_->readRegister(REG_LEAVING_WATER_TEMP);
        status.leaving_water_temp = raw_to_temp(raw);
    } catch (const ModbusException&) {
        ok = false;
    }

    // Read DHW tank temp (0x00CE)
    try {
        raw = modbus_client_->readRegister(REG_DHW_TANK_TEMP);
        status.dhw_tank_temp = raw_to_temp(raw);
    } catch (const ModbusException&) {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to read DHW tank temp");
        ok = false;
    }

    // Read running mode (0x002C)
    try {
        raw = modbus_client_->readRegister(REG_RUNNING_MODE);
        status.running_mode = raw;
        // Update current_mode from the device (may have been changed externally)
        if (raw == 0 || raw == 1 || raw == 2) {
            current_mode_ = raw;
        }
    } catch (const ModbusException&) {
        ok = false;
    }

    // Read running status (0x002D)
    try {
        raw = modbus_client_->readRegister(REG_RUNNING_STATUS);
        status.running_status = raw;
        status.is_running = (raw != MODE_STATUS_OFF);
    } catch (const ModbusException&) {
        ok = false;
    }

    // Read DHW target (0x0194)
    try {
        raw = modbus_client_->readRegister(REG_DHW_TARGET);
        status.dhw_target = raw_to_temp(raw);
        if (!saved_targets_initialized_) {
            saved_dhw_target_ = status.dhw_target;
        }
    } catch (const ModbusException&) {
        ok = false;
    }

    // Read heating target (0x0191)
    try {
        raw = modbus_client_->readRegister(REG_HEATING_TARGET);
        status.heating_target = raw_to_temp(raw);
        last_heating_target_ = status.heating_target;
        if (!saved_targets_initialized_) {
            saved_heating_target_ = status.heating_target;
        }
    } catch (const ModbusException&) {
        status.heating_target = last_heating_target_;
    }

    // Read DHW priority (0x02BF) - only for status reporting, not for control
    try {
        raw = modbus_client_->readRegister(REG_DHW_PRIORITY);
        status.dhw_priority = (raw != 0);
        // Note: current_priority_ is managed by applyControlLogic(), not read from device
        // to prevent external changes from overriding the desired working mode priority
    } catch (const ModbusException&) {
        ok = false;
    }

    // Read power monitoring registers
    try {
        int16_t ac_current_raw = modbus_client_->readRegister(REG_AC_CURRENT);
        int16_t dc_current_raw = modbus_client_->readRegister(REG_DC_CURRENT);
        int16_t ac_voltage_raw = modbus_client_->readRegister(REG_AC_VOLTAGE);
        int16_t dc_voltage_raw = modbus_client_->readRegister(REG_DC_VOLTAGE);

        // Apply scaling factors per device spec
        status.ac_current = static_cast<float>(ac_current_raw) * 2.0f;   // Actual = Display * 2
        status.dc_current = static_cast<float>(dc_current_raw) * 4.0f;   // Actual = Display * 4
        status.ac_voltage = static_cast<float>(ac_voltage_raw);           // Actual = Display
        status.dc_voltage = static_cast<float>(dc_voltage_raw) / 2.0f;   // Actual = Display / 2
        status.ac_power = status.ac_voltage * status.ac_current;          // Power in Watts (AC)
    } catch (const ModbusException&) {
        status.ac_current = 0.0f;
        status.dc_current = 0.0f;
        status.ac_voltage = 0.0f;
        status.dc_voltage = 0.0f;
        status.ac_power = 0.0f;
    }

    status.device_online = ok;
    if (ok) {
        saved_targets_initialized_ = true;
    }
    return ok;
}

void ControlLoop::processCommands() {
    if (!cmd_queue_) return;

    Command cmd;
    int cmd_count = 0;
    while (cmd_queue_->pop(cmd)) {
        cmd_count++;
        WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "Processing command #%d - type=%d", cmd_count,
               static_cast<int>(cmd.type));

        switch (cmd.type) {
            case CommandType::CMD_SET_DHW_TEMP:
                WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_DHW_TEMP, temp=%.1f C", cmd.float_val);
                setDhwTarget(cmd.float_val);
                saved_dhw_target_ = cmd.float_val;
                break;

            case CommandType::CMD_SET_HEATING_TEMP:
                WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_HEATING_TEMP, temp=%.1f C", cmd.float_val);
                setHeatingTarget(cmd.float_val);
                last_heating_target_ = cmd.float_val;
                saved_heating_target_ = cmd.float_val;
                break;

            case CommandType::CMD_SET_PRIORITY:
                WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_PRIORITY, pri_val=%d", cmd.int_val);
                try {
                    modbus_client_->writeRegister(REG_DHW_PRIORITY, static_cast<uint16_t>(cmd.int_val));
                    current_priority_ = (cmd.int_val == 1) ? PriorityMode::Dhw : PriorityMode::Heating;
                    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Priority set to %s",
                           cmd.int_val == 1 ? "DHW" : "Heating");
                } catch (const ModbusException& e) {
                    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set priority register: %s", e.what());
                }
                break;

            case CommandType::CMD_SET_RUNNING_MODE:
                WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_RUNNING_MODE, mode=%d", cmd.int_val);
                desired_working_mode_ = cmd.int_val;

                {
                    int target_device_mode;
                    switch (cmd.int_val) {
                        case 0:  target_device_mode = MODE_SET_OFF;       break;
                        case 1:  target_device_mode = MODE_SET_HEAT_DHW;   break;
                        case 2:  target_device_mode = MODE_SET_HEAT_DHW;  break;
                        case 3:  target_device_mode = MODE_SET_HEAT_DHW;  break;
                        default: target_device_mode = MODE_SET_HEAT_DHW;   break;
                    }

                    if (setRunningMode(target_device_mode) == 0) {
                        current_mode_ = target_device_mode;
                        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Working mode set to %d (device mode=%d)",
                               cmd.int_val, target_device_mode);
                    } else {
                        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set running mode");
                        break;
                    }

                    // Override target temperatures and priority based on working mode
                    switch (cmd.int_val) {
                        case 1:  // DHW-only
                            WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "DHW-only mode, setting heating target to min (%.1f)",
                                   static_cast<float>(HEATING_TARGET_MIN));
                            setHeatingTarget(HEATING_TARGET_MIN);
                            try {
                                modbus_client_->writeRegister(REG_DHW_PRIORITY, 1);
                                current_priority_ = PriorityMode::Dhw;
                                WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "DHW-only mode, set DHW priority");
                            } catch (const ModbusException& e) {
                                WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set DHW priority: %s", e.what());
                            }
                            break;
                        case 2:  // Heating-only
                            WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Heating-only mode, setting DHW target to min (%.1f)",
                                   static_cast<float>(DHW_TARGET_MIN));
                            setDhwTarget(DHW_TARGET_MIN);
                            try {
                                modbus_client_->writeRegister(REG_DHW_PRIORITY, 0);
                                current_priority_ = PriorityMode::Heating;
                                WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Heating-only mode, cleared DHW priority");
                            } catch (const ModbusException& e) {
                                WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to clear DHW priority: %s", e.what());
                            }
                            break;
                        case 3:  // DHW+Heating
                            WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "DHW+Heating mode, restoring targets (DHW=%.1f, Heating=%.1f)",
                                   saved_dhw_target_, saved_heating_target_);
                            setDhwTarget(saved_dhw_target_);
                            setHeatingTarget(saved_heating_target_);
                            try {
                                modbus_client_->writeRegister(REG_DHW_PRIORITY, 1);
                                current_priority_ = PriorityMode::Dhw;
                                WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "DHW+Heating mode, set DHW priority");
                            } catch (const ModbusException& e) {
                                WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set DHW priority: %s", e.what());
                            }
                            break;
                        case 0:  // Off: no target or priority changes needed
                            break;
                    }
                }
                break;

            default:
                WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Unknown command type: %d",
                       static_cast<int>(cmd.type));
                break;
        }
    }
    if (cmd_count > 0) {
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Batch complete - processed %d command(s)", cmd_count);
    }
}

void ControlLoop::applyControlLogic(StatusSnapshot& status) {
    // Determine what device mode we should be in
    int desired_device_mode;
    switch (desired_working_mode_) {
        case 0:  desired_device_mode = MODE_SET_OFF;       break;
        case 1:  desired_device_mode = MODE_SET_HEAT_DHW;  break;
        case 2:  desired_device_mode = MODE_SET_HEAT_DHW;  break;
        case 3:  desired_device_mode = MODE_SET_HEAT_DHW;  break;
        default: desired_device_mode = MODE_SET_HEAT_DHW;   break;
    }

    // Only change mode if it doesn't match
    if (current_mode_ != desired_device_mode) {
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Correcting device mode from %d to %d (desired working mode=%d)",
               current_mode_, desired_device_mode, desired_working_mode_);
        if (setRunningMode(desired_device_mode) == 0) {
            current_mode_ = desired_device_mode;
        }
    }

    // Enforce target temperature overrides based on working mode
    switch (desired_working_mode_) {
        case 1:  // DHW-only: keep heating target at minimum
            if (status.heating_target > HEATING_TARGET_MIN + 0.5f) {
                WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "DHW-only enforcing heating target min (%.1f, was %.1f)",
                       static_cast<float>(HEATING_TARGET_MIN), status.heating_target);
                setHeatingTarget(HEATING_TARGET_MIN);
            }
            break;
        case 2:  // Heating-only: keep DHW target at minimum
            if (status.dhw_target > DHW_TARGET_MIN + 0.5f) {
                WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Heating-only enforcing DHW target min (%.1f, was %.1f)",
                       static_cast<float>(DHW_TARGET_MIN), status.dhw_target);
                setDhwTarget(DHW_TARGET_MIN);
            }
            break;
        case 3:  // DHW+Heating: ensure user's saved targets are active
            if (fabsf(status.dhw_target - saved_dhw_target_) > 0.5f) {
                WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Restoring DHW target (%.1f, was %.1f)",
                       saved_dhw_target_, status.dhw_target);
                setDhwTarget(saved_dhw_target_);
            }
            if (fabsf(status.heating_target - saved_heating_target_) > 0.5f) {
                WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Restoring heating target (%.1f, was %.1f)",
                       saved_heating_target_, status.heating_target);
                setHeatingTarget(saved_heating_target_);
            }
            break;
    }

    // Enforce priority based on working mode
    WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "Enforcing priority for working_mode=%d, current_priority=%s",
           desired_working_mode_, current_priority_ == PriorityMode::Dhw ? "Dhw" : "Heating");
    switch (desired_working_mode_) {
        case 1:  // DHW-only: must have DHW priority
        case 3:  // DHW+Heating: must have DHW priority
            if (current_priority_ != PriorityMode::Dhw) {
                WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Mode %d enforcing DHW priority", desired_working_mode_);
                try {
                    modbus_client_->writeRegister(REG_DHW_PRIORITY, 1);
                    current_priority_ = PriorityMode::Dhw;
                    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Priority now set to DHW");
                } catch (const ModbusException& e) {
                    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set DHW priority: %s", e.what());
                }
            }
            break;
        case 2:  // Heating-only: must have no DHW priority
            if (current_priority_ != PriorityMode::Heating) {
                WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Heating-only mode clearing DHW priority");
                try {
                    modbus_client_->writeRegister(REG_DHW_PRIORITY, 0);
                    current_priority_ = PriorityMode::Heating;
                    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Priority now set to Heating");
                } catch (const ModbusException& e) {
                    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to clear DHW priority: %s", e.what());
                }
            }
            break;
    }
}

void ControlLoop::threadFunc() {
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Thread started");

    // Publish initial status snapshot immediately so web server has data
    {
        StatusSnapshot initial_status{};
        if (readStatus(initial_status)) {
            initial_status.device_online = true;
            initial_status.working_mode = desired_working_mode_;
            if (status_queue_) {
                if (!status_queue_->push(initial_status)) {
                    WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP, "Status queue full, dropping snapshot");
                }
            }
        }
    }

    // Main loop
    while (!stop_requested_.load()) {
        auto start_time = std::chrono::steady_clock::now();

        // Check connection and reconnect if needed
        if (!modbus_client_ || !modbus_client_->isConnected()) {
            WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP, "Not connected, attempting to reconnect...");

            int retries = 0;
            while (!stop_requested_.load() && retries < MODBUS_MAX_RETRIES) {
                if (modbus_client_->connect()) {
                    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Reconnected successfully");
                    break;
                }
                retries++;
                std::unique_lock<std::mutex> lock(kick_mutex_);
                kick_cond_.wait_for(lock, std::chrono::seconds(MODBUS_RECONNECT_INTERVAL_S),
                                    [this]() { return stop_requested_.load(); });
            }

            if (retries >= MODBUS_MAX_RETRIES) {
                WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to reconnect after %d attempts", retries);
                std::unique_lock<std::mutex> lock(kick_mutex_);
                kick_cond_.wait_for(lock, std::chrono::seconds(MODBUS_RECONNECT_INTERVAL_S),
                                    [this]() { return stop_requested_.load(); });
                continue;
            }
        }

        // Process any pending commands
        processCommands();

        // Read current status
        StatusSnapshot status{};
        if (readStatus(status)) {
            // Apply control logic
            applyControlLogic(status);

            // Set working mode for status reporting
            status.working_mode = desired_working_mode_;

            // Publish status to queue
            if (status_queue_) {
                if (!status_queue_->push(status)) {
                    WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP, "Status queue full, dropping snapshot");
                }
            }
        } else {
            WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to read status");
            // Publish offline status
            status.device_online = false;
            status.working_mode = desired_working_mode_;
            if (status_queue_) {
                if (!status_queue_->push(status)) {
                    WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP, "Status queue full, dropping snapshot");
                }
            }
        }

        // Calculate sleep time to maintain interval
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        auto sleep_ms = static_cast<long>(CONTROL_LOOP_INTERVAL_S) * 1000 - elapsed_ms;

        if (sleep_ms > 0) {
            std::unique_lock<std::mutex> lock(kick_mutex_);
            const uint64_t observed_generation = kick_generation_;
            kick_cond_.wait_for(lock, std::chrono::milliseconds(sleep_ms),
                                [this, observed_generation]() {
                                    return stop_requested_.load()
                                        || kick_generation_ != observed_generation
                                        || (cmd_queue_ && !cmd_queue_->empty());
                                });
        }
    }

    // Disconnect
    if (modbus_client_) {
        modbus_client_->disconnect();
    }

    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Thread stopped");
    running_.store(false);
}

} // namespace windmi