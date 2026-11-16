/** @file core/Scheduler.hpp
 * Daily local-time schedule. State is copied under a lock; no mutable references
 * cross the web/control thread boundary. Gaps retain the last device settings.
 */
#ifndef WINDMI_CORE_SCHEDULER_HPP
#define WINDMI_CORE_SCHEDULER_HPP
#include "utils/Platform.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace windmi {
struct ScheduleRange {
    int startMinutes = 0;
    int endMinutes = 0;
    int mode = 0; // Working mode: 0 Off, 1 DHW, 2 Heating, 3 Both
    float heatingTemp = 40.0f;
    float dhwTemp = 50.0f;
};
struct ScheduleState {
    bool enabled = false;
    bool paused = false;
    std::vector<ScheduleRange> ranges;
    uint64_t revision = 0;
    std::string instanceId;
    bool persistenceError = false;
};
class Scheduler {
public:
    using PersistenceHandler = std::function<bool(const ScheduleState&)>;
    Scheduler();
    ScheduleState snapshot() const;
    std::vector<ScheduleRange> getRanges() const;
    bool isEnabled() const;
    bool isPaused() const;
    bool setRanges(const std::vector<ScheduleRange>& ranges);
    bool setEnabled(bool enabled);
    // Atomic configuration replacement; pause is preserved until explicit Resume.
    bool replace(bool enabled, const std::vector<ScheduleRange>& ranges);
    bool pause();
    bool resume();
    // Startup only: validated restoration before accepting HTTP requests.
    bool restore(const ScheduleState& state);
    void setPersistenceHandler(PersistenceHandler handler);
    bool evaluate(int minutes, ScheduleRange& out) const;
    static bool evaluate(const ScheduleState& state, int minutes, ScheduleRange& out);
    static bool validateRanges(const std::vector<ScheduleRange>& ranges);
private:
    bool commitLocked(ScheduleState candidate);
    mutable Mutex mutex_;
    ScheduleState state_;
    PersistenceHandler persist_;
};
}
#endif
