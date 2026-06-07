/**
 * @file src/core/StatusMonitor.cpp
 * @brief Status monitor implementation
 */

#include "core/StatusMonitor.hpp"

namespace windmi {

StatusMonitor::StatusMonitor() : status_{}, valid_{false}
{}

void StatusMonitor::update(const StatusSnapshot& snapshot)
{
  windmi::LockGuard lock(mutex_);
  status_ = snapshot;
  valid_.store(true);
}

bool StatusMonitor::get(StatusSnapshot& snapshot)
{
  windmi::LockGuard lock(mutex_);
  snapshot = status_;
  return valid_.load();
}

StatusSnapshot StatusMonitor::get() const
{
  windmi::LockGuard lock(mutex_);
  return status_;
}

bool StatusMonitor::isValid() const
{
  return valid_.load();
}

void StatusMonitor::reset()
{
  windmi::LockGuard lock(mutex_);
  status_ = StatusSnapshot{};
  valid_.store(false);
}

} // namespace windmi
