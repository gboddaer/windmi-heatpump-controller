/**
 * @file src/core/ControlLoop.cpp
 * @brief Control loop implementation matching master branch control_loop.c
 */

#include "core/ControlLoop.hpp"
#include "modbus/ModbusClient.hpp"
#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>

namespace windmi {

// ---- CmdQueue implementation ----

CmdQueue::CmdQueue() : head_(0), tail_(0) {}

bool CmdQueue::push(const Command& cmd) {
    uint32_t next_tail = (tail_ + 1) % CAPACITY;
    if (next_tail == head_) return false;  // Full
    buf_[tail_] = cmd;
    tail_ = next_tail;
    return true;
}

bool CmdQueue::pop(Command& cmd) {
    if (head_ == tail_) return false;  // Empty
    cmd = buf_[head_];
    head_ = (head_ + 1) % CAPACITY;
    return true;
}

bool CmdQueue::empty() const {
    return head_ == tail_;
}

// ---- StatusQueue implementation ----

StatusQueue::StatusQueue() : head_(0), tail_(0) {}

bool StatusQueue::push(const StatusSnapshot& item) {
    uint32_t next_tail = (tail_ + 1) % CAPACITY;
    if (next_tail == head_) return false;  // Full
    buf_[tail_] = item;
    tail_ = next_tail;
    return true;
}

bool StatusQueue::pop(StatusSnapshot& item) {
    if (head_ == tail_) return false;  // Empty
    item = buf_[head_];
    head_ = (head_ + 1) % CAPACITY;
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
    , kicked_(false)
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

bool ControlLoop::start(ModbusClient* client, CmdQueue* cmd_queue, StatusQueue* status_queue) {
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
    kicked_ = true;
    kick_cond_.notify_one();
}

int ControlLoop::setRunningMode(int mode) {
    if (!modbus_client_) return -1;

    // Validate: only 0, 1, 2 are valid device modes
    if (mode < 0 || mode > 2) {
        fprintf(stderr, "Invalid device mode %d: only 0 (Off), 1 (Cool+DHW), 2 (Heat+DHW) supported\n", mode);
        return -1;
    }

    try {
        modbus_client_->writeRegister(REG_RUNNING_MODE, static_cast<uint16_t>(mode));
        printf("Control loop: Set running mode to %d\n", mode);
        return 0;
    } catch (const ModbusException& e) {
        fprintf(stderr, "Failed to set running mode to %d: %s\n", mode, e.what());
        return -1;
    }
}

int ControlLoop::setDhwTarget(float temp) {
    if (!modbus_client_) return -1;

    int16_t raw_temp = temp_to_raw(temp);
    try {
        modbus_client_->writeRegister(REG_DHW_TARGET, static_cast<uint16_t>(raw_temp));
        printf("Control loop: Set DHW target to %.1f C\n", temp);
        return 0;
    } catch (const ModbusException& e) {
        fprintf(stderr, "Failed to set DHW target to %.1f: %s\n", temp, e.what());
        return -1;
    }
}

int ControlLoop::setHeatingTarget(float temp) {
    if (!modbus_client_) return -1;

    int16_t raw_temp = temp_to_raw(temp);
    try {
        modbus_client_->writeRegister(REG_HEATING_TARGET, static_cast<uint16_t>(raw_temp));
        printf("Control loop: Set heating target to %.1f C\n", temp);
        return 0;
    } catch (const ModbusException& e) {
        fprintf(stderr, "Failed to set heating target to %.1f: %s\n", temp, e.what());
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
        fprintf(stderr, "Failed to read DHW tank temp\n");
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

    // Read DHW priority (0x02BF)
    try {
        raw = modbus_client_->readRegister(REG_DHW_PRIORITY);
        status.dhw_priority = (raw != 0);
        current_priority_ = status.dhw_priority ? PriorityMode::Dhw : PriorityMode::Heating;
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
        printf("Control loop: Processing command #%d - type=%d", cmd_count,
               static_cast<int>(cmd.type));

        switch (cmd.type) {
            case CommandType::CMD_SET_DHW_TEMP:
                printf(" (CMD_SET_DHW_TEMP, temp=%.1f C)\n", cmd.float_val);
                setDhwTarget(cmd.float_val);
                saved_dhw_target_ = cmd.float_val;
                break;

            case CommandType::CMD_SET_HEATING_TEMP:
                printf(" (CMD_SET_HEATING_TEMP, temp=%.1f C)\n", cmd.float_val);
                setHeatingTarget(cmd.float_val);
                last_heating_target_ = cmd.float_val;
                saved_heating_target_ = cmd.float_val;
                break;

            case CommandType::CMD_SET_PRIORITY:
                printf(" (CMD_SET_PRIORITY, pri_val=%d)\n", cmd.int_val);
                try {
                    modbus_client_->writeRegister(REG_DHW_PRIORITY, static_cast<uint16_t>(cmd.int_val));
                    current_priority_ = (cmd.int_val == 1) ? PriorityMode::Dhw : PriorityMode::Heating;
                    printf("Control loop: Priority set to %s\n",
                           cmd.int_val == 1 ? "DHW" : "Heating");
                } catch (const ModbusException& e) {
                    fprintf(stderr, "Control loop: Failed to set priority register: %s\n", e.what());
                }
                break;

            case CommandType::CMD_SET_RUNNING_MODE:
                printf(" (CMD_SET_RUNNING_MODE, mode=%d)\n", cmd.int_val);
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
                        printf("Control loop: Working mode set to %d (device mode=%d)\n",
                               cmd.int_val, target_device_mode);
                    } else {
                        fprintf(stderr, "Control loop: Failed to set running mode\n");
                        break;
                    }

                    // Override target temperatures and priority based on working mode
                    switch (cmd.int_val) {
                        case 1:  // DHW-only
                            printf("Control loop: DHW-only mode, setting heating target to min (%.1f)\n",
                                   static_cast<float>(HEATING_TARGET_MIN));
                            setHeatingTarget(HEATING_TARGET_MIN);
                            try {
                                modbus_client_->writeRegister(REG_DHW_PRIORITY, 1);
                                current_priority_ = PriorityMode::Dhw;
                                printf("Control loop: DHW-only mode, set DHW priority\n");
                            } catch (const ModbusException& e) {
                                fprintf(stderr, "Control loop: Failed to set DHW priority: %s\n", e.what());
                            }
                            break;
                        case 2:  // Heating-only
                            printf("Control loop: Heating-only mode, setting DHW target to min (%.1f)\n",
                                   static_cast<float>(DHW_TARGET_MIN));
                            setDhwTarget(DHW_TARGET_MIN);
                            try {
                                modbus_client_->writeRegister(REG_DHW_PRIORITY, 0);
                                current_priority_ = PriorityMode::Heating;
                                printf("Control loop: Heating-only mode, cleared DHW priority\n");
                            } catch (const ModbusException& e) {
                                fprintf(stderr, "Control loop: Failed to clear DHW priority: %s\n", e.what());
                            }
                            break;
                        case 3:  // DHW+Heating
                            printf("Control loop: DHW+Heating mode, restoring targets (DHW=%.1f, Heating=%.1f)\n",
                                   saved_dhw_target_, saved_heating_target_);
                            setDhwTarget(saved_dhw_target_);
                            setHeatingTarget(saved_heating_target_);
                            try {
                                modbus_client_->writeRegister(REG_DHW_PRIORITY, 1);
                                current_priority_ = PriorityMode::Dhw;
                                printf("Control loop: DHW+Heating mode, set DHW priority\n");
                            } catch (const ModbusException& e) {
                                fprintf(stderr, "Control loop: Failed to set DHW priority: %s\n", e.what());
                            }
                            break;
                        case 0:  // Off: no target or priority changes needed
                            break;
                    }
                }
                break;

            default:
                fprintf(stderr, "Control loop: Unknown command type: %d\n",
                       static_cast<int>(cmd.type));
                break;
        }
    }
    if (cmd_count > 0) {
        printf("Control loop: Batch complete - processed %d command(s)\n", cmd_count);
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
        printf("Control loop: Correcting device mode from %d to %d (desired working mode=%d)\n",
               current_mode_, desired_device_mode, desired_working_mode_);
        if (setRunningMode(desired_device_mode) == 0) {
            current_mode_ = desired_device_mode;
        }
    }

    // Enforce target temperature overrides based on working mode
    switch (desired_working_mode_) {
        case 1:  // DHW-only: keep heating target at minimum
            if (status.heating_target > HEATING_TARGET_MIN + 0.5f) {
                printf("Control loop: DHW-only enforcing heating target min (%.1f, was %.1f)\n",
                       static_cast<float>(HEATING_TARGET_MIN), status.heating_target);
                setHeatingTarget(HEATING_TARGET_MIN);
            }
            break;
        case 2:  // Heating-only: keep DHW target at minimum
            if (status.dhw_target > DHW_TARGET_MIN + 0.5f) {
                printf("Control loop: Heating-only enforcing DHW target min (%.1f, was %.1f)\n",
                       static_cast<float>(DHW_TARGET_MIN), status.dhw_target);
                setDhwTarget(DHW_TARGET_MIN);
            }
            break;
        case 3:  // DHW+Heating: ensure user's saved targets are active
            if (fabsf(status.dhw_target - saved_dhw_target_) > 0.5f) {
                printf("Control loop: Restoring DHW target (%.1f, was %.1f)\n",
                       saved_dhw_target_, status.dhw_target);
                setDhwTarget(saved_dhw_target_);
            }
            if (fabsf(status.heating_target - saved_heating_target_) > 0.5f) {
                printf("Control loop: Restoring heating target (%.1f, was %.1f)\n",
                       saved_heating_target_, status.heating_target);
                setHeatingTarget(saved_heating_target_);
            }
            break;
    }

    // Enforce priority based on working mode
    switch (desired_working_mode_) {
        case 1:  // DHW-only: must have DHW priority
        case 3:  // DHW+Heating: must have DHW priority
            if (current_priority_ != PriorityMode::Dhw) {
                printf("Control loop: Mode %d enforcing DHW priority\n", desired_working_mode_);
                try {
                    modbus_client_->writeRegister(REG_DHW_PRIORITY, 1);
                    current_priority_ = PriorityMode::Dhw;
                } catch (const ModbusException& e) {
                    fprintf(stderr, "Control loop: Failed to set DHW priority: %s\n", e.what());
                }
            }
            break;
        case 2:  // Heating-only: must have no DHW priority
            if (current_priority_ != PriorityMode::Heating) {
                printf("Control loop: Heating-only mode clearing DHW priority\n");
                try {
                    modbus_client_->writeRegister(REG_DHW_PRIORITY, 0);
                    current_priority_ = PriorityMode::Heating;
                } catch (const ModbusException& e) {
                    fprintf(stderr, "Control loop: Failed to clear DHW priority: %s\n", e.what());
                }
            }
            break;
    }
}

void ControlLoop::threadFunc() {
    printf("Control loop thread started\n");

    // Publish initial status snapshot immediately so web server has data
    {
        StatusSnapshot initial_status{};
        if (readStatus(initial_status)) {
            initial_status.device_online = true;
            initial_status.working_mode = desired_working_mode_;
            if (status_queue_) {
                if (!status_queue_->push(initial_status)) {
                    fprintf(stderr, "Control loop: Status queue full, dropping snapshot\n");
                }
            }
        }
    }

    // Main loop
    while (!stop_requested_.load()) {
        auto start_time = std::chrono::steady_clock::now();

        // Check connection and reconnect if needed
        if (!modbus_client_ || !modbus_client_->isConnected()) {
            printf("Control loop: Not connected, attempting to reconnect...\n");

            int retries = 0;
            while (!stop_requested_.load() && retries < MODBUS_MAX_RETRIES) {
                if (modbus_client_->connect()) {
                    printf("Control loop: Reconnected successfully\n");
                    break;
                }
                retries++;
                std::this_thread::sleep_for(std::chrono::seconds(MODBUS_RECONNECT_INTERVAL_S));
            }

            if (retries >= MODBUS_MAX_RETRIES) {
                fprintf(stderr, "Control loop: Failed to reconnect after %d attempts\n", retries);
                std::this_thread::sleep_for(std::chrono::seconds(MODBUS_RECONNECT_INTERVAL_S));
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
                    fprintf(stderr, "Control loop: Status queue full, dropping snapshot\n");
                }
            }
        } else {
            fprintf(stderr, "Control loop: Failed to read status\n");
            // Publish offline status
            status.device_online = false;
            status.working_mode = desired_working_mode_;
            if (status_queue_) {
                if (!status_queue_->push(status)) {
                    fprintf(stderr, "Control loop: Status queue full, dropping snapshot\n");
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
            kicked_ = false;
            kick_cond_.wait_for(lock, std::chrono::milliseconds(sleep_ms),
                                [this]() { return kicked_ || stop_requested_.load(); });
        }
    }

    // Disconnect
    if (modbus_client_) {
        modbus_client_->disconnect();
    }

    printf("Control loop thread stopped\n");
    running_.store(false);
}

} // namespace windmi