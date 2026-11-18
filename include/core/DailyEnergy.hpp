#ifndef WINDMI_CORE_DAILYENERGY_HPP
#define WINDMI_CORE_DAILYENERGY_HPP

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace windmi {

/** @brief Longest sample gap that still counts as continuous operation (2 minutes). */
inline constexpr int64_t kDailyEnergyMaxGapMs = 120000;
/** @brief How often today's total is written to settings (1 hour). */
inline constexpr int64_t kDailyEnergyPersistIntervalMs = 3600000;

/**
 * @brief Accumulates estimated input energy for the current local day.
 *
 * Pure value type: every method takes the wall-clock time in epoch milliseconds,
 * so the caller owns the clock and tests are exact. There are no reads of
 * system time here beyond formatting a key for calendar-day comparison.
 *
 * The figure is an estimate built from the uncalibrated AC-power hypothesis
 * (raw current / 2, power factor 0.90). It is not a meter reading.
 */
class DailyEnergyAccumulator
{
public:
  /** @brief "YYYY-MM-DD" in local time, or "" when the timestamp is unusable. */
  static std::string dayKeyFor(int64_t wallMs)
  {
    if (wallMs <= 0)
    {
      return std::string();
    }
    const std::time_t seconds = static_cast<std::time_t>(wallMs / 1000);
    std::tm tmBuf{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tmBuf, &seconds);
#else
    localtime_r(&seconds, &tmBuf);
#endif
    char buffer[11];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", tmBuf.tm_year + 1900, tmBuf.tm_mon + 1,
                  tmBuf.tm_mday);
    return std::string(buffer);
  }

  /**
   * @brief Adds one power sample, integrating the gap since the previous sample.
   *
   * The first sample of a day integrates nothing: there is no interval to
   * attribute energy to. A gap longer than kDailyEnergyMaxGapMs integrates
   * nothing either, because a long silence is not evidence of continuous draw.
   */
  void observe(int64_t wallMs, double watts)
  {
    rollIfNeeded(wallMs);
    if (mHasSample)
    {
      const int64_t elapsedMs = wallMs - mLastSampleMs;
      if (elapsedMs > 0 && elapsedMs <= kDailyEnergyMaxGapMs && watts > 0.0)
      {
        mKwh += watts * static_cast<double>(elapsedMs) / 3.6e9;
      }
    }
    mLastSampleMs = wallMs;
    mHasSample = true;
    mHasToday = true;
  }

  /**
   * @brief Restores a saved total, but only when it belongs to the current day.
   */
  void seed(int64_t wallMs, double kwh, const std::string& savedDayKey)
  {
    rollIfNeeded(wallMs);
    if (savedDayKey.empty() || savedDayKey != mDayKey)
    {
      return;
    }
    mKwh = kwh > 0.0 ? kwh : 0.0;
    mHasToday = true;
  }

  /** @brief Rolls the day without adding energy (used while the device is offline). */
  void advance(int64_t wallMs)
  {
    rollIfNeeded(wallMs);
  }

  /** @brief True when no save has happened yet, or an hour has passed since the last one. */
  bool persistDue(int64_t wallMs) const
  {
    return !mHasPersisted || wallMs - mLastPersistMs >= kDailyEnergyPersistIntervalMs;
  }

  void markPersisted(int64_t wallMs)
  {
    mLastPersistMs = wallMs;
    mHasPersisted = true;
  }

  double kwhToday() const
  {
    return mKwh;
  }

  bool hasToday() const
  {
    return mHasToday;
  }

  std::string storedDayKey() const
  {
    return mDayKey;
  }

private:
  void rollIfNeeded(int64_t wallMs)
  {
    const std::string key = dayKeyFor(wallMs);
    if (key == mDayKey)
    {
      return;
    }
    mDayKey = key;
    mKwh = 0.0;
    mHasToday = false;
    mHasSample = false;
    mLastSampleMs = 0;
  }

  double mKwh = 0.0;
  std::string mDayKey;
  bool mHasToday = false;
  bool mHasSample = false;
  int64_t mLastSampleMs = 0;
  bool mHasPersisted = false;
  int64_t mLastPersistMs = 0;
};

} // namespace windmi

#endif // WINDMI_CORE_DAILYENERGY_HPP
