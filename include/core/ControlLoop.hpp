/**
 * @file core/ControlLoop.hpp
 * @brief Control loop for heat pump management
 *
 * StatusSnapshot matches master branch status_snapshot_t exactly.
 * Control loop logic matches master branch control_loop.c.
 */

#ifndef WINDMI_CORE_CONTROL_LOOP_HPP
#define WINDMI_CORE_CONTROL_LOOP_HPP

#include <cstdint>
#include <memory>
#include <atomic>
#include <functional>
#include <string>
#include "utils/Platform.hpp"

namespace windmi {

// Forward declarations
class IModbusClient;

/**
 * @brief Command types for the control loop
 *
 * These match the C cmd_type_t enum from spsc_queue.h.
 */
enum class CommandType : uint8_t {
    CMD_SET_DHW_TEMP = 0,
    CMD_SET_HEATING_TEMP = 1,
    CMD_SET_PRIORITY = 2,
    CMD_SET_RUNNING_MODE = 3
};

/**
 * @brief Command structure matching C cmd_t from spsc_queue.h
 */
struct Command {
    CommandType type;
    float float_val = 0.0f;
    int int_val = 0;
};

/**
 * @brief Status snapshot structure matching C status_snapshot_t from spsc_queue.h
 *
 * Field names and types match master branch exactly.
 * - running_mode: device register value (0=Off, 1=Cool+DHW, 2=Heat+DHW)
 * - running_status: device register value (0=Off, 1=Cool, 2=Heat, 4=DHW, 7=Defrost, 20=Antifreeze)
 * - dhw_priority: true=DHW priority, false=heating priority
 * - is_running: true if device is active (running_status != 0)
 * - working_mode: application-level (0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating)
 */
struct StatusSnapshot {
    float outdoor_temp = 0.0f;
    float indoor_temp = 0.0f;
    float leaving_water_temp = 0.0f;
    float entering_water_temp = 0.0f;  // From REG_ENTERING_WATER_TEMP (0x0003, 0.1°C)
    float dhw_tank_temp = 0.0f;
    float dhw_target = 0.0f;
    float heating_target = 0.0f;
    int running_mode = 0;        // From REG_RUNNING_MODE (0x002C)
    int running_status = 0;      // From REG_RUNNING_STATUS (0x002D)
    bool dhw_priority = false;
    bool is_running = false;
    bool device_online = false;
    // Power monitoring
    float ac_current = 0.0f;       // AC current in Amps (raw * 2)
    float dc_current = 0.0f;       // DC current in Amps (raw * 4)
    float ac_voltage = 0.0f;       // AC voltage in Volts (raw)
    float dc_voltage = 0.0f;       // DC voltage in Volts (raw / 2)
    float ac_power_va = 0.0f;      // AC apparent power in VA (ac_voltage * ac_current)
    float ac_power_w = 0.0f;       // AC real power in Watts (estimated: VA * power_factor)
    bool power_valid = false;      // True if both AC current and AC voltage were read
    
    // Diagnostic registers
    float compressor_freq = 0.0f;       // Actual compressor frequency in Hz
    float water_flow = 0.0f;            // Water flow in m³/h (from 0x102A, raw/100)
    int unit_capacity_kw = 0;           // Unit capacity in kW (4/6/8/10/12/14/16)
    int actual_capacity_output = 0;     // Actual capacity output (from 0x1004)
    int odu_input_status = 0;           // Outdoor unit input status bit flags (from 0x101F)
    int compressor_runtime_h = 0;       // Compressor runtime in hours (from 0x0174)
    int pump_runtime_h = 0;             // Pump runtime in hours (from 0x0176)
    
    // COP estimation (calculated from water flow + delta-T + power)
    float heat_output_w = 0.0f;         // Estimated heat output in Watts
    float cop = 0.0f;                    // Coefficient of Performance (heat_out / power_in)
    bool cop_valid = false;             // True if COP calculation had valid inputs
    
    // Working mode (0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating)
    int working_mode = 0;
};

/**
 * @brief Priority mode enum (internal, not a register value)
 *
 */
enum class PriorityMode : uint8_t {
    Dhw,       // DHW priority
    Heating    // Heating priority
};

/**
 * @brief SPSC command queue interface
 *
 * Used by WebServer to push commands and by ControlLoop to consume them.
 * The web server is the single producer; the control loop is the single consumer.
 */
class CmdQueue {
public:
    static constexpr size_t CAPACITY = 16;

    CmdQueue();
    bool push(const Command& cmd);
    bool pop(Command& cmd);
    bool empty() const;

private:
    Command buf_[CAPACITY];
    alignas(64) std::atomic<uint32_t> head_;
    alignas(64) std::atomic<uint32_t> tail_;
};

/**
 * @brief SPSC status queue with overwrite (ring buffer)
 *
 * Used by ControlLoop to publish status snapshots and by WebServer to consume them.
 * The control loop is the single producer; the web server is the single consumer.
 * 
 * When full, push() overwrites the oldest entry (ring buffer behavior) to prevent
 * "queue full" warnings when the consumer is idle. This is appropriate for status
 * monitoring where only the latest snapshot matters.
 */
class StatusQueue {
public:
    static constexpr size_t CAPACITY = 32;

    StatusQueue();
    
    /**
     * @brief Push a status snapshot (overwrites oldest if full)
     * @param item Snapshot to push
     * @return Always true (never fails due to full queue)
     */
    bool push(const StatusSnapshot& item);
    
    /**
     * @brief Pop the oldest snapshot
     * @param item Output parameter
     * @return true if snapshot was available, false if queue was empty
     */
    bool pop(StatusSnapshot& item);
    
    /**
     * @brief Get the latest snapshot without consuming it
     * @param item Output parameter
     * @return true if snapshot was available, false if queue was empty
     */
    bool latest(StatusSnapshot& item);

private:
    StatusSnapshot buf_[CAPACITY];
    alignas(64) std::atomic<uint32_t> head_;
    alignas(64) std::atomic<uint32_t> tail_;
    alignas(64) std::atomic<uint32_t> write_index_;  // For ring buffer overwrite
};

/**
 * @brief Control loop class for heat pump management
 *
 * Manages the control loop thread, processes commands from the command queue,
 * reads device status via Modbus, applies control logic, and publishes
 * status snapshots to the status queue.
 *
 * Matches master branch control_loop.c functionality exactly.
 */
class ControlLoop {
public:
    ControlLoop();
    ~ControlLoop();

    /**
     * @brief Start the control loop thread
     * @param client IModbusClient to use for device communication
     * @param cmd_queue Command queue to consume
     * @param status_queue Status queue to publish to
     * @return true if successful, false otherwise
     */
    bool start(IModbusClient* client, CmdQueue* cmd_queue, StatusQueue* status_queue);

    void stop();
    bool isRunning() const;

    /**
     * @brief Kick the control loop to wake it immediately
     *
     * Called when a command is pushed to the cmd_queue so the
     * control loop processes it without waiting for the next poll.
     */
    void kick();

private:
    void threadFunc();
    bool readStatus(StatusSnapshot& status);
    void processCommands();
    void applyControlLogic(StatusSnapshot& status);
    int setRunningMode(int mode);
    int setDhwTarget(float temp);
    int setHeatingTarget(float temp);

    // Modbus and queue pointers (not owned)
    IModbusClient* modbus_client_;
    CmdQueue* cmd_queue_;
    StatusQueue* status_queue_;

    // Thread management
    std::unique_ptr<windmi::Thread> thread_;
    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;

    // Kick mechanism
    windmi::Mutex kick_mutex_;
    windmi::ConditionVariable kick_cond_;
    uint64_t kick_generation_;

    // Control state (matches master branch)
    PriorityMode current_priority_;
    int current_mode_;              // Device mode we last set (0, 1, or 2)
    int desired_working_mode_;      // Application-level (0-3)
    float last_heating_target_;
    float saved_dhw_target_;
    float saved_heating_target_;
    bool saved_targets_initialized_;
};

} // namespace windmi

#endif // WINDMI_CORE_CONTROL_LOOP_HPP