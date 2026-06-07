/**
 * @file src/core/StatusMonitor.cpp
 * @brief Status monitor implementation
 */

#include "core/StatusMonitor.hpp"

namespace windmi {

StatusMonitor::StatusMonitor() : mStatus{}, mValid{false}
{}

void StatusMonitor::update(const StatusSnapshot& snapshot)
{
  windmi::LockGuard lock(mMutex);
  mStatus = snapshot;
  mValid.store(true);
}

bool StatusMonitor::get(StatusSnapshot& snapshot)
{
  windmi::LockGuard lock(mMutex);
  snapshot = mStatus;
  return mValid.load();
}

StatusSnapshot StatusMonitor::get() const
{
  windmi::LockGuard lock(mMutex);
  return mStatus;
}

bool StatusMonitor::isValid() const
{
  return mValid.load();
}

void StatusMonitor::reset()
{
  windmi::LockGuard lock(mMutex);
  mStatus = StatusSnapshot{};
  mValid.store(false);
}

} // namespace windmi
