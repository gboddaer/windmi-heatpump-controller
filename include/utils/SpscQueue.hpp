/**
 * @file utils/SpscQueue.hpp
 * @brief Single Producer Single Consumer queue
 */

#ifndef WINDMI_UTILS_SPSC_QUEUE_HPP_
#define WINDMI_UTILS_SPSC_QUEUE_HPP_

#include <cstddef>
#include <atomic>
#include <vector>

namespace windmi {

/**
 * @brief SPSC queue for command storage
 *
 * Template-based SPSC queue with compile-time size.
 */
template <typename T, size_t Size = 16> class SpscQueue
{
public:
  static_assert(Size > 0, "Queue size must be greater than 0");
  static_assert((Size & (Size - 1)) == 0, "Queue size must be power of 2");

  /**
   * @brief Constructor
   */
  SpscQueue();

  /**
   * @brief Push an item to the queue
   * @param item Item to push
   * @return true if successful, false if queue is full
   */
  bool push(const T& item);

  /**
   * @brief Pop an item from the queue
   * @param item Output parameter for item
   * @return true if successful, false if queue is empty
   */
  bool pop(T& item);

  /**
   * @brief Check if queue is empty
   * @return true if empty, false otherwise
   */
  bool empty() const;

  /**
   * @brief Check if queue is full
   * @return true if full, false otherwise
   */
  bool full() const;

  /**
   * @brief Get queue size
   * @return Queue size
   */
  constexpr size_t size() const
  {
    return Size;
  }

  /**
   * @brief Clear the queue
   */
  void clear();

private:
  T mItems[Size];
  std::atomic<size_t> mHead{0};
  std::atomic<size_t> mTail{0};
  size_t mMask{Size - 1};
};

} // namespace windmi

#endif // WINDMI_UTILS_SPSC_QUEUE_HPP
