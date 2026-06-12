/**
 * @file utils/ScheduleConfig.hpp
 * @brief Schedule JSON persistence
 *
 * Loads and saves the schedule to a JSON file.
 * Schema: { "enabled": bool, "ranges": [...] }
 */

#ifndef WINDMI_UTILS_SCHEDULE_CONFIG_HPP
#define WINDMI_UTILS_SCHEDULE_CONFIG_HPP

#include <string>
#include <vector>
#include <optional>
#include "core/Scheduler.hpp"

namespace windmi {

/**
 * @brief Schedule file persistence
 *
 * Reads and writes schedule.json containing enabled flag and range list.
 */
class ScheduleConfig {
public:
    ScheduleConfig();

    /**
     * @brief Load schedule from file
     * @param filename Path to schedule JSON file
     * @return true if file was loaded successfully
     */
    bool loadFromFile(const std::string& filename);

    /**
     * @brief Save schedule to file
     * @param filename Path to schedule JSON file
     * @return true if save was successful
     */
    bool saveToFile(const std::string& filename) const;

    /**
     * @brief Get enabled state
     */
    bool isEnabled() const;

    /**
     * @brief Set enabled state
     */
    void setEnabled(bool enabled);

    /**
     * @brief Get schedule ranges
     */
    const std::vector<ScheduleRange>& getRanges() const;

    /**
     * @brief Set schedule ranges
     * @return true if ranges are valid
     */
    bool setRanges(const std::vector<ScheduleRange>& ranges);

private:
    bool enabled_ = false;
    std::vector<ScheduleRange> ranges_;
};

} // namespace windmi

#endif // WINDMI_UTILS_SCHEDULE_CONFIG_HPP
