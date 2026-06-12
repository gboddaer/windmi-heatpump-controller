/**
 * @file core/Scheduler.hpp
 * @brief Weekly schedule evaluation engine
 *
 * Manages time-based automation for heating and DHW.
 * Evaluates the current time against a list of schedule ranges
 * and returns the active range. Integrates with ControlLoop
 * to push mode/temperature commands automatically.
 */

#ifndef WINDMI_CORE_SCHEDULER_HPP
#define WINDMI_CORE_SCHEDULER_HPP

#include <cstdint>
#include <vector>
#include <string>

namespace windmi {

/**
 * @brief A single time range in the schedule
 *
 * Each range defines a mode and optional temperatures for a time window.
 * Ranges must not overlap and should cover 00:00–24:00 (0–1440 minutes).
 */
struct ScheduleRange {
    int startMinutes = 0;       // Minutes from midnight (0–1439)
    int endMinutes = 0;         // Minutes from midnight (1–1440, 1440 = midnight)
    int mode = 0;               // 0=OFF, 1=DHW Only, 2=Heating Only, 3=Heating+DHW
    float heatingTemp = 40.0f;  // Heating target temperature (°C)
    float dhwTemp = 50.0f;      // DHW target temperature (°C)
};

/**
 * @brief Schedule evaluation engine
 *
 * Holds a list of non-overlapping time ranges and evaluates which
 * range is active for a given time of day. Supports enable/disable
 * and pause/resume for user override handling.
 */
class Scheduler {
public:
    Scheduler();

    /**
     * @brief Set the schedule ranges (replaces all existing ranges)
     * @param ranges List of ranges (will be sorted by start time)
     * @return true if all ranges are valid, false otherwise
     */
    bool setRanges(const std::vector<ScheduleRange>& ranges);

    /**
     * @brief Get the current schedule ranges
     */
    const std::vector<ScheduleRange>& getRanges() const;

    /**
     * @brief Check if the schedule is enabled
     */
    bool isEnabled() const;

    /**
     * @brief Enable or disable the schedule
     */
    void setEnabled(bool enabled);

    /**
     * @brief Check if the schedule is paused (user override)
     */
    bool isPaused() const;

    /**
     * @brief Pause the schedule (set by manual user changes)
     */
    void pause();

    /**
     * @brief Resume the schedule
     */
    void resume();

    /**
     * @brief Evaluate which range is active at the given time
     * @param minutes Minutes from midnight (0–1440)
     * @param out Output: the active range
     * @return true if a range was found, false if schedule is disabled/paused or no match
     */
    bool evaluate(int minutes, ScheduleRange& out) const;

    /**
     * @brief Validate ranges: no overlaps, sorted, within bounds
     */
    static bool validateRanges(const std::vector<ScheduleRange>& ranges);

private:
    std::vector<ScheduleRange> ranges_;
    bool enabled_ = false;
    bool paused_ = false;
};

} // namespace windmi

#endif // WINDMI_CORE_SCHEDULER_HPP
