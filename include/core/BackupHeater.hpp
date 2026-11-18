#ifndef WINDMI_CORE_BACKUP_HEATER_HPP
#define WINDMI_CORE_BACKUP_HEATER_HPP
#include "core/DiagnosticRegisters.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
namespace windmi {
// Validation evidence only: never asserts heater ON/OFF or accumulates runtime.
struct BackupHeaterValidation {
    bool measurement_valid = false;
    std::string reason = "missing_temperature";
    std::optional<double> before_c, after_c, delta_c, flow_m3h, estimated_added_heat_w;
    std::optional<uint64_t> sample_span_ms;
};
inline BackupHeaterValidation estimateBackupHeater(const DiagnosticSnapshot& snapshot, bool online) {
    BackupHeaterValidation result;
    if (!online) { result.reason = "offline"; return result; }
    const auto& before = snapshot.readings[diagnosticIndex(REG_BEFORE_BACKUP_HEATER_TEMP)];
    const auto& after = snapshot.readings[diagnosticIndex(REG_LEAVING_WATER_TEMP)];
    const auto& flow = snapshot.readings[diagnosticIndex(REG_WATER_FLOW)];
    auto temperature = [](const DiagnosticReading& r) -> std::optional<double> {
        if (!r.raw) return std::nullopt;
        const auto raw = static_cast<int16_t>(*r.raw);
        // Manual -40 C sentinel, plus a broad plausibility guard (not a setpoint limit).
        if (raw <= -400 || raw > 1000) return std::nullopt;
        return raw / 10.0;
    };
    result.before_c = temperature(before);
    result.after_c = temperature(after);
    if (!before.raw || !after.raw) return result;
    if (!result.before_c || !result.after_c) { result.reason = "invalid_temperature"; return result; }
    result.delta_c = *result.after_c - *result.before_c;
    if (!flow.raw) { result.reason = "missing_flow"; return result; }
    const double m3h = static_cast<int16_t>(*flow.raw) / 100.0;
    if (m3h < 0 || m3h > 10) { result.reason = "invalid_flow"; return result; }
    result.flow_m3h = m3h;
    if (m3h <= .01) { result.reason = "no_flow"; return result; }
    const auto earliest = std::min({before.sampled_at_ms, after.sampled_at_ms, flow.sampled_at_ms});
    const auto latest = std::max({before.sampled_at_ms, after.sampled_at_ms, flow.sampled_at_ms});
    result.sample_span_ms = latest - earliest;
    if (latest > snapshot.observed_at_ms) { result.reason = "sample_time"; return result; }
    if (*result.sample_span_ms > 5000) { result.reason = "sample_skew"; return result; }
    if (snapshot.observed_at_ms - earliest > 10000) { result.reason = "stale_sample"; return result; }
    // Water-only estimate. Sensor offsets, thermal lag, glycol and other heat paths
    // must be checked against ORIS query item 36 or an independent measurement.
    result.estimated_added_heat_w = (m3h * 1000.0 / 3600.0) * 4186.0 * *result.delta_c;
    result.measurement_valid = true;
    result.reason = "ok";
    return result;
}
}
#endif
