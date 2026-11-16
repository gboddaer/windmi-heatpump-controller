#ifndef WINDMI_UTILS_SCHEDULE_CONFIG_HPP
#define WINDMI_UTILS_SCHEDULE_CONFIG_HPP
#include "core/Scheduler.hpp"
#include <string>
namespace windmi {
// Single-threaded file/JSON adapter; live synchronized ownership belongs to Scheduler.
class ScheduleConfig {
public:
    bool loadFromFile(const std::string& filename);
    bool saveToFile(const std::string& filename) const;
    bool isEnabled() const { return state_.enabled; }
    bool isPaused() const { return state_.paused; }
    void setEnabled(bool enabled) { state_.enabled = enabled; }
    const std::vector<ScheduleRange>& getRanges() const { return state_.ranges; }
    bool setRanges(const std::vector<ScheduleRange>& ranges);
    bool setState(const ScheduleState& state);
    ScheduleState state() const { return state_; }
    static bool parseJson(const std::string& json, ScheduleState& out, std::string& error);
    static bool parseEnabledJson(const std::string& json, bool& enabled);
    static std::string toJson(const ScheduleState& state, bool runtime = false);
private:
    ScheduleState state_;
};
}
#endif
