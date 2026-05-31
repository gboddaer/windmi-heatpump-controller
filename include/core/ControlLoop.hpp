/**
 * @file core/ControlLoop.hpp
 * @brief Control loop for heat pump management
 */

#ifndef WINDMI_CORE_CONTROL_LOOP_HPP
#define WINDMI_CORE_CONTROL_LOOP_HPP

#include <cstdint>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace windmi {

/**
 * @brief Command types for the control loop
 */
enum class CommandType : uint8_t {
    CMD_SET_DHW_TEMP = 1,
    CMD_SET_HEATING_TEMP,
    CMD_SET_PRIORITY,
    CMD_SET_RUNNING_MODE
};

/**
 * @brief Status snapshot structure
 */
struct StatusSnapshot {
    float dhw_tank_temp;
    float dhw_target;
    float heating_temperature;
    float heating_target;
    float outdoor_temp;
    float leaving_water_temp;
    int mode;
    int running_status;
    int priority;
    int status;
    bool device_online;
    float ac_current;
    float dc_current;
    float ac_voltage;
    float dc_voltage;
    float ac_power;
    int working_mode;
};

/**
 * @brief Control loop class for heat pump management
 * 
 * Manages the control loop thread, processes commands,
 * and maintains status updates.
 */
class ControlLoop {
public:
    /**
     * @brief Callback type for status updates
     */
    using StatusCallback = std::function<void(const StatusSnapshot&)>;

    /**
     * @brief Constructor
     * @param status_cb Optional callback for status updates
     */
    explicit ControlLoop(StatusCallback status_cb = nullptr);

    /**
     * @brief Destructor
     */
    ~ControlLoop();

    /**
     * @brief Start the control loop thread
     * @return true if successful, false otherwise
     */
    bool start();

    /**
     * @brief Stop the control loop thread
     */
    void stop();

    /**
     * @brief Check if the control loop is running
     * @return true if running, false otherwise
     */
    bool isRunning() const;

    /**
     * @brief Get the current status snapshot
     * @param snapshot Output parameter for status
     * @return true if snapshot was retrieved, false otherwise
     */
    bool getStatus(StatusSnapshot& snapshot);

    /**
     * @brief Enqueue a command
     * @param type Command type
     * @param float_val Float value for command
     * @param int_val Integer value for command
     * @return true if command was enqueued, false otherwise
     */
    bool enqueueCommand(CommandType type, float float_val, int int_val);

    /**
     * @brief Set the Modbus client pointer
     * @param client_ptr Pointer to Modbus client
     */
    void setModbusClient(void* client_ptr);

private:
    /**
     * @brief Thread function for control loop
     */
    void threadFunc();

    /**
     * @brief Read current status from device
     * @param snapshot Output parameter for status
     * @return true if successful, false otherwise
     */
    bool readStatus(StatusSnapshot& snapshot);

    /**
     * @brief Apply control logic
     */
    void applyControlLogic();

    // Member variables
    StatusCallback status_callback_;
    void* modbus_client_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;
    StatusSnapshot current_status_;
    std::mutex status_mutex_;
};

} // namespace windmi

#endif // WINDMI_CORE_CONTROL_LOOP_HPP
