/**
 * @file src/core/Scheduler.cpp
 * @brief Schedule evaluation engine implementation
 */

#include "core/Scheduler.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"

#include <algorithm>

namespace windmi {

Scheduler::Scheduler() = default;

bool Scheduler::validateRanges(const std::vector<ScheduleRange>& ranges) {
    if (ranges.empty()) return false;

    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& r = ranges[i];
        if (r.startMinutes < 0 || r.startMinutes >= 1440) return false;
        if (r.endMinutes < 0 || r.endMinutes > 1440) return false;
        // start == end is invalid (zero-length range)
        if (r.startMinutes == r.endMinutes) return false;
        if (r.mode < 0 || r.mode > 3) return false;
    }

    // Check for overlaps and that they're sorted by start
    for (size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].startMinutes < ranges[i - 1].startMinutes) return false;
        // If previous range doesn't wrap midnight, check normal overlap
        if (ranges[i - 1].startMinutes < ranges[i - 1].endMinutes) {
            if (ranges[i].startMinutes < ranges[i - 1].endMinutes) return false;
        }
    }

    return true;
}

bool Scheduler::setRanges(const std::vector<ScheduleRange>& ranges) {
    if (!validateRanges(ranges)) {
        WINDMI_LOG_ERROR(LOG_TAG_SCHEDULER, "Invalid schedule ranges provided");
        return false;
    }

    // Store sorted copy
    ranges_ = ranges;
    std::sort(ranges_.begin(), ranges_.end(),
              [](const ScheduleRange& a, const ScheduleRange& b) {
                  return a.startMinutes < b.startMinutes;
              });

    WINDMI_LOG_INFO(LOG_TAG_SCHEDULER, "Schedule updated with %zu ranges", ranges_.size());
    return true;
}

const std::vector<ScheduleRange>& Scheduler::getRanges() const {
    return ranges_;
}

bool Scheduler::isEnabled() const {
    return enabled_;
}

void Scheduler::setEnabled(bool enabled) {
    enabled_ = enabled;
    WINDMI_LOG_INFO(LOG_TAG_SCHEDULER, "Schedule %s", enabled ? "enabled" : "disabled");
}

bool Scheduler::isPaused() const {
    return paused_;
}

void Scheduler::pause() {
    paused_ = true;
    WINDMI_LOG_INFO(LOG_TAG_SCHEDULER, "Schedule paused (user override)");
}

void Scheduler::resume() {
    paused_ = false;
    WINDMI_LOG_INFO(LOG_TAG_SCHEDULER, "Schedule resumed");
}

bool Scheduler::evaluate(int minutes, ScheduleRange& out) const {
    if (!enabled_ || paused_ || ranges_.empty()) {
        return false;
    }

    // Normalize minutes to 0-1439
    minutes = ((minutes % 1440) + 1440) % 1440;

    for (const auto& range : ranges_) {
        if (range.startMinutes < range.endMinutes) {
            // Normal range: start < end
            if (minutes >= range.startMinutes && minutes < range.endMinutes) {
                out = range;
                return true;
            }
        } else {
            // Wrapped range (crosses midnight): e.g., 1380 (23:00) to 360 (06:00)
            if (minutes >= range.startMinutes || minutes < range.endMinutes) {
                out = range;
                return true;
            }
        }
    }

    return false;
}

} // namespace windmi
