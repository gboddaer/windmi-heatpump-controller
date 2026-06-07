/**
 * @file core/StatusMonitor.hpp
 * @brief Thread-safe status monitoring
 */

#ifndef WINDMI_CORE_STATUS_MONITOR_HPP
#define WINDMI_CORE_STATUS_MONITOR_HPP

#include "core/ControlLoop.hpp"
#include "utils/Platform.hpp"
#include <atomic>

namespace windmi {

/**
 * @brief Thread-safe status monitor
 *
 * Maintains a current status snapshot with thread-safe
 * access from multiple threads.
 */
class StatusMonitor {
public:
    StatusMonitor();
    StatusMonitor(const StatusMonitor&) = delete;
    StatusMonitor& operator=(const StatusMonitor&) = delete;

    void update(const StatusSnapshot& snapshot);
    bool get(StatusSnapshot& snapshot);
    StatusSnapshot get() const;
    bool isValid() const;
    void reset();

private:
    StatusSnapshot status_;
    mutable windmi::Mutex mutex_;
    std::atomic<bool> valid_;
};

} // namespace windmi

#endif // WINDMI_CORE_STATUS_MONITOR_HPP