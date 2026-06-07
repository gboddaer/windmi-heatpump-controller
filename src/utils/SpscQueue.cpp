/**
 * @file src/utils/SpscQueue.cpp
 * @brief SPSC queue implementation
 */

#include "utils/SpscQueue.hpp"

namespace windmi {

template <typename T, size_t Size> SpscQueue<T, Size>::SpscQueue()
{
  static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
}

template <typename T, size_t Size> bool SpscQueue<T, Size>::push(const T& item)
{
  size_t current_tail = mTail.load(std::memory_order_relaxed);
  size_t next_tail = (current_tail + 1) & mMask;

  if (next_tail == mHead.load(std::memory_order_acquire))
  {
    return false; // Queue is full
  }

  mItems[current_tail] = item;
  mTail.store(next_tail, std::memory_order_release);
  return true;
}

template <typename T, size_t Size> bool SpscQueue<T, Size>::pop(T& item)
{
  size_t current_head = mHead.load(std::memory_order_relaxed);

  if (current_head == mTail.load(std::memory_order_acquire))
  {
    return false; // Queue is empty
  }

  item = mItems[current_head];
  mHead.store((current_head + 1) & mMask, std::memory_order_release);
  return true;
}

template <typename T, size_t Size> bool SpscQueue<T, Size>::empty() const
{
  return mHead.load(std::memory_order_acquire) == mTail.load(std::memory_order_acquire);
}

template <typename T, size_t Size> bool SpscQueue<T, Size>::full() const
{
  size_t next_tail = (mTail.load(std::memory_order_acquire) + 1) & mMask;
  return next_tail == mHead.load(std::memory_order_acquire);
}

template <typename T, size_t Size> void SpscQueue<T, Size>::clear()
{
  mHead.store(0, std::memory_order_release);
  mTail.store(0, std::memory_order_release);
}

// Explicit template instantiations
template class SpscQueue<int8_t, 16>;
template class SpscQueue<int16_t, 16>;
template class SpscQueue<int32_t, 16>;
template class SpscQueue<int64_t, 16>;
template class SpscQueue<float, 16>;
template class SpscQueue<double, 16>;

} // namespace windmi
