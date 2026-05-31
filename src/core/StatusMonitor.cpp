/**
 * @file src/core/StatusMonitor.cpp
 * @brief Status monitor implementation
 */

#include "core/StatusMonitor.hpp"

namespace windmi {

StatusMonitor::StatusMonitor()
    : valid_{false} {
    // Initialize to default values
    status_.dhw_tank_temp = 0.0f;
    status_.dhw_target = 0.0f;
    status_.heating_temperature = 0.0f;
    status_.heating_target = 0.0f;
    status_.outdoor_temp = 0.0f;
    status_.leaving_water_temp = 0.0f;
    status_.mode = 0;
    status_.running_status = 0;
    status_.priority = 0;
    status_.status = 0;
    status_.device_online = false;
    status_.ac_current = 0.0f;
    status_.dc_current = 0.0f;
    status_.ac_voltage = 0.0f;
    status_.dc_voltage = 0.0f;
    status_.ac_power = 0.0f;
    status_.working_mode = 0;
}

void StatusMonitor::update(const StatusSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = snapshot;
    valid_.store(true);
}

bool StatusMonitor::get(StatusSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot = status_;
    return valid_.load();
}

StatusSnapshot StatusMonitor::get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

bool StatusMonitor::isValid() const {
    return valid_.load();
}

void StatusMonitor::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = StatusSnapshot{};
    valid_.store(false);
}

} // namespace windmi
