#ifndef WINDMI_UTILS_SCHEDULE_CONFIG_HPP
#define WINDMI_UTILS_SCHEDULE_CONFIG_HPP
#include "core/Scheduler.hpp"
#include <string>
namespace windmi {
// Single-threaded file/JSON adapter; live synchronized ownership belongs to Scheduler.
class ScheduleConfig
{
public:
  bool loadFromFile(const std::string& filename);
  bool saveToFile(const std::string& filename) const;
  bool isEnabled() const
  {
    return mState.enabled;
  }
  bool isPaused() const
  {
    return mState.paused;
  }
  void setEnabled(bool enabled)
  {
    mState.enabled = enabled;
  }
  const std::vector<ScheduleRange>& getRanges() const
  {
    return mState.ranges;
  }
  bool setRanges(const std::vector<ScheduleRange>& ranges);
  bool setState(const ScheduleState& state);
  ScheduleState state() const
  {
    return mState;
  }
  bool hasDefaults() const
  {
    return mHasDefaults;
  }
  ScheduleState stateWithLegacyDefaults(const ScheduleDefaults& fallback) const
  {
    auto result = mState;
    if (!mHasDefaults)
      result.defaults = Scheduler::validateDefaults(fallback) ? fallback : ScheduleDefaults{};
    return result;
  }
  static bool parseJson(const std::string& json, ScheduleState& out, std::string& error,
                        bool* defaultsPresent = nullptr);
  static bool parseDefaultsJson(const std::string& json, ScheduleDefaults& out, std::string& error);
  static bool parseEnabledJson(const std::string& json, bool& enabled);
  static std::string toJson(const ScheduleState& state, bool runtime = false);

private:
  ScheduleState mState;
  bool mHasDefaults = false;
};
} // namespace windmi
#endif
