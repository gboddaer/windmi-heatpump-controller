#ifndef WINDMI_CORE_CYCLE_COP_HPP
#define WINDMI_CORE_CYCLE_COP_HPP

#include <cmath>
#include <cstdint>

namespace windmi {
enum class CopService { None, Heating, Dhw };

struct CyclePowerSample {
    CopService service = CopService::None;
    bool compressor_running = false;
    bool protection = false;
    bool valid = true;
    double input_w = 0;
    double output_w = 0; // signed: defrost removes heat
};

struct CompletedCycleCop {
    bool available = false;
    double cop = 0;
    double electric_wh = 0;
    double heat_wh = 0;
};

// Left-endpoint integration: a reading applies until the next sample. At a
// boundary, the preceding interval belongs to the preceding service, not the new
// one. Results are energy ratios, not arithmetic averages of instantaneous COP.
// Never publish startup fragments or cycles with missing observations.
class CycleCop {
public:
    void update(uint64_t now_ms, CyclePowerSample sample) {
        sample.valid = sample.valid && std::isfinite(sample.input_w) &&
            std::isfinite(sample.output_w) && sample.input_w >= 0;
        const bool continuous = seen_ && now_ms >= previous_ms_ &&
            now_ms - previous_ms_ <= 90000;
        if (active_ != CopService::None) {
            valid_ = valid_ && continuous && previous_.valid && sample.valid;
            if (continuous) {
                const double hours = (now_ms - previous_ms_) / 3600000.0;
                input_wh_ += previous_.input_w * hours;
                heat_wh_ += previous_.output_w * hours;
            }
            if (!sample.protection && (!sample.compressor_running || sample.service != active_)) {
                if (valid_ && now_ms - started_ms_ >= 60000 && input_wh_ > 0) {
                    auto& result = active_ == CopService::Heating ? heating_ : dhw_;
                    result = {true, heat_wh_ / input_wh_, input_wh_, heat_wh_};
                }
                active_ = CopService::None;
            }
        }
        if (active_ == CopService::None && !sample.protection &&
            sample.compressor_running && sample.service != CopService::None) {
            active_ = sample.service;
            started_ms_ = now_ms;
            input_wh_ = heat_wh_ = 0;
            valid_ = continuous && previous_.valid && sample.valid;
        }
        previous_ = sample;
        previous_ms_ = now_ms;
        seen_ = true;
    }

    const CompletedCycleCop& heating() const { return heating_; }
    const CompletedCycleCop& dhw() const { return dhw_; }

private:
    CopService active_ = CopService::None;
    bool seen_ = false;
    bool valid_ = false;
    uint64_t started_ms_ = 0;
    uint64_t previous_ms_ = 0;
    CyclePowerSample previous_;
    double input_wh_ = 0;
    double heat_wh_ = 0;
    CompletedCycleCop heating_, dhw_;
};
} // namespace windmi
#endif
