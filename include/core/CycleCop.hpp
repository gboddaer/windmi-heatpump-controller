#ifndef WINDMI_CORE_CYCLE_COP_HPP
#define WINDMI_CORE_CYCLE_COP_HPP

#include <cmath>
#include <cstdint>

namespace windmi {
enum class CopService { None, Heating, Dhw };

struct CyclePowerSample
{
  CopService service = CopService::None;
  bool compressor_running = false;
  bool protection = false;
  bool valid = true;
  double input_w = 0;
  double output_w = 0; // signed: defrost removes heat
};

struct CompletedCycleCop
{
  bool available = false;
  double cop = 0;
  double electric_wh = 0;
  double heat_wh = 0;
};

// Left-endpoint integration: a reading applies until the next sample. At a
// boundary, the preceding interval belongs to the preceding service, not the new
// one. Results are energy ratios, not arithmetic averages of instantaneous COP.
// Never publish startup fragments or cycles with missing observations.
class CycleCop
{
public:
  void update(uint64_t now_ms, CyclePowerSample sample)
  {
    sample.valid = sample.valid && std::isfinite(sample.input_w) &&
                   std::isfinite(sample.output_w) && sample.input_w >= 0;
    const bool continuous = mSeen && now_ms >= mPreviousMs && now_ms - mPreviousMs <= 90000;
    if (mActive != CopService::None)
    {
      mValid = mValid && continuous && mPrevious.valid && sample.valid;
      if (continuous)
      {
        const double hours = (now_ms - mPreviousMs) / 3600000.0;
        mInputWh += mPrevious.input_w * hours;
        mHeatWh += mPrevious.output_w * hours;
      }
      if (!sample.protection && (!sample.compressor_running || sample.service != mActive))
      {
        if (mValid && now_ms - mStartedMs >= 60000 && mInputWh > 0)
        {
          auto& result = mActive == CopService::Heating ? mHeating : mDhw;
          result = {true, mHeatWh / mInputWh, mInputWh, mHeatWh};
        }
        mActive = CopService::None;
      }
    }
    if (mActive == CopService::None && !sample.protection && sample.compressor_running &&
        sample.service != CopService::None)
    {
      mActive = sample.service;
      mStartedMs = now_ms;
      mInputWh = mHeatWh = 0;
      mValid = continuous && mPrevious.valid && sample.valid;
    }
    mPrevious = sample;
    mPreviousMs = now_ms;
    mSeen = true;
  }

  const CompletedCycleCop& heating() const
  {
    return mHeating;
  }
  const CompletedCycleCop& dhw() const
  {
    return mDhw;
  }

private:
  CopService mActive = CopService::None;
  bool mSeen = false;
  bool mValid = false;
  uint64_t mStartedMs = 0;
  uint64_t mPreviousMs = 0;
  CyclePowerSample mPrevious;
  double mInputWh = 0;
  double mHeatWh = 0;
  CompletedCycleCop mHeating, mDhw;
};
} // namespace windmi
#endif
