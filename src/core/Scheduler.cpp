#include "core/Scheduler.hpp"
#include "config.h"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <utility>
namespace windmi {
Scheduler::Scheduler() {
    static std::atomic<unsigned long long> serial{0};
    state_.instanceId = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                        "-" + std::to_string(++serial);
}
bool Scheduler::validateRanges(const std::vector<ScheduleRange>& ranges) {
    if (ranges.size() > 1440) return false;
    std::array<bool, 1440> covered{};
    int previousStart = -1;
    for (const auto& r : ranges) {
        if (r.startMinutes < 0 || r.startMinutes >= 1440 || r.endMinutes < 0 || r.endMinutes > 1440 ||
            r.startMinutes == r.endMinutes || r.startMinutes < previousStart || r.mode < 0 || r.mode > 3 ||
            !std::isfinite(r.heatingTemp) || r.heatingTemp < HEATING_TEMP_MIN || r.heatingTemp > HEATING_TEMP_MAX ||
            !std::isfinite(r.dhwTemp) || r.dhwTemp < DHW_TEMP_MIN || r.dhwTemp > DHW_TEMP_MAX) return false;
        previousStart = r.startMinutes;
        for (int minute = 0; minute < 1440; ++minute) {
            const bool inside = r.startMinutes < r.endMinutes ?
                minute >= r.startMinutes && minute < r.endMinutes :
                minute >= r.startMinutes || minute < r.endMinutes;
            if (!inside) continue;
            if (covered[minute]) return false;
            covered[minute] = true;
        }
    }
    return true;
}
ScheduleState Scheduler::snapshot() const { LockGuard lock(mutex_); return state_; }
std::vector<ScheduleRange> Scheduler::getRanges() const { return snapshot().ranges; }
bool Scheduler::isEnabled() const { return snapshot().enabled; }
bool Scheduler::isPaused() const { return snapshot().paused; }
void Scheduler::setPersistenceHandler(PersistenceHandler handler) {
    LockGuard lock(mutex_);
    persist_ = std::move(handler);
}
bool Scheduler::commitLocked(ScheduleState candidate) {
    if (!validateRanges(candidate.ranges) || (candidate.enabled && candidate.ranges.empty())) return false;
    candidate.revision = state_.revision + 1;
    candidate.persistenceError = false;
    try {
        if (persist_ && !persist_(candidate)) return false;
    } catch (...) {
        WINDMI_LOG_ERROR(LOG_TAG_SCHEDULER, "Schedule persistence failed; candidate not published");
        return false;
    }
    state_ = std::move(candidate);
    return true;
}
bool Scheduler::replace(bool enabled, const std::vector<ScheduleRange>& ranges) {
    LockGuard lock(mutex_);
    auto candidate = state_;
    candidate.enabled = enabled;
    candidate.ranges = ranges;
    return commitLocked(std::move(candidate));
}
bool Scheduler::setRanges(const std::vector<ScheduleRange>& ranges) {
    LockGuard lock(mutex_);
    auto candidate = state_;
    candidate.ranges = ranges;
    if (ranges.empty()) candidate.enabled = false;
    return commitLocked(std::move(candidate));
}
bool Scheduler::setEnabled(bool enabled) {
    LockGuard lock(mutex_);
    auto candidate = state_;
    candidate.enabled = enabled;
    return commitLocked(std::move(candidate));
}
bool Scheduler::pause() {
    LockGuard lock(mutex_);
    if (!state_.enabled || (state_.paused && !state_.persistenceError)) return true;
    auto candidate = state_;
    candidate.paused = true;
    if (commitLocked(std::move(candidate))) return true;
    // A failed disk must never let automation fight a manual command.
    state_.paused = true;
    state_.persistenceError = true;
    ++state_.revision;
    WINDMI_LOG_ERROR(LOG_TAG_SCHEDULER, "Manual override paused scheduling in memory, but saving the pause failed");
    return false;
}
bool Scheduler::resume() {
    LockGuard lock(mutex_);
    auto candidate = state_;
    candidate.paused = false;
    return commitLocked(std::move(candidate));
}
bool Scheduler::restore(const ScheduleState& state) {
    if (!validateRanges(state.ranges) || (state.enabled && state.ranges.empty())) return false;
    LockGuard lock(mutex_);
    auto candidate = state;
    candidate.instanceId = state_.instanceId;
    candidate.revision = state_.revision + 1;
    candidate.persistenceError = false;
    state_ = std::move(candidate);
    return true;
}
bool Scheduler::evaluate(int minutes, ScheduleRange& out) const { return evaluate(snapshot(), minutes, out); }
bool Scheduler::evaluate(const ScheduleState& state, int minutes, ScheduleRange& out) {
    if (!state.enabled || state.paused) return false;
    minutes = ((minutes % 1440) + 1440) % 1440;
    for (const auto& r : state.ranges) {
        const bool inside = r.startMinutes < r.endMinutes ? minutes >= r.startMinutes && minutes < r.endMinutes :
                                                           minutes >= r.startMinutes || minutes < r.endMinutes;
        if (inside) { out = r; return true; }
    }
    return false;
}
}
