/**
 * @file core/StatusMonitor.h
 * @brief Thread-safe status monitoring
 */

#ifndef WINDMI_CORE_STATUS_MONITOR_H
#define WINDMI_CORE_STATUS_MONITOR_H

#include "core/ControlLoop.hpp"
#include <mutex>
#include <atomic>
#include <condition_variable>

namespace windmi {

/**
 * @brief Thread-safe status monitor
 * 
 * Maintains a current status snapshot with thread-safe
 * access from multiple threads.
 */
class StatusMonitor {
public:
    /**
     * @brief Constructor
     */
    StatusMonitor();

    /**
     * @brief Copy constructor (deleted)
     */
    StatusMonitor(const StatusMonitor&) = delete;

    /**
     * @brief Assignment operator (deleted)
     */
    StatusMonitor& operator=(const StatusMonitor&) = delete;

    /**
     * @brief Update status with new snapshot
     * @param snapshot New status snapshot
     */
    void update(const StatusSnapshot& snapshot);

    /**
     * @brief Get current status snapshot
     * @param snapshot Output parameter for status
     * @return true if successful, false otherwise
     */
    bool get(StatusSnapshot& snapshot);

    /**
     * @brief Get current status snapshot (copy)
     * @return Current status snapshot
     */
    StatusSnapshot get() const;

    /**
     * @brief Check if status is valid
     * @return true if valid, false otherwise
     */
    bool isValid() const;

    /**
     * @brief Reset status to default values
     */
    void reset();

private:
    StatusSnapshot status_;
    mutable std::mutex mutex_;
    std::atomic<bool> valid_;
};

} // namespace windmi

#endif // WINDMI_CORE_STATUS_MONITOR_H
