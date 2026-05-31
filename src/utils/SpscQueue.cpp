/**
 * @file src/utils/SpscQueue.cpp
 * @brief SPSC queue implementation
 */

#include "utils/SpscQueue.hpp"

namespace windmi {

template<typename T, size_t Size>
SpscQueue<T, Size>::SpscQueue() {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
}

template<typename T, size_t Size>
bool SpscQueue<T, Size>::push(const T& item) {
    size_t current_tail = tail_.load(std::memory_order_relaxed);
    size_t next_tail = (current_tail + 1) & mask_;
    
    if (next_tail == head_.load(std::memory_order_acquire)) {
        return false;  // Queue is full
    }
    
    items_[current_tail] = item;
    tail_.store(next_tail, std::memory_order_release);
    return true;
}

template<typename T, size_t Size>
bool SpscQueue<T, Size>::pop(T& item) {
    size_t current_head = head_.load(std::memory_order_relaxed);
    
    if (current_head == tail_.load(std::memory_order_acquire)) {
        return false;  // Queue is empty
    }
    
    item = items_[current_head];
    head_.store((current_head + 1) & mask_, std::memory_order_release);
    return true;
}

template<typename T, size_t Size>
bool SpscQueue<T, Size>::empty() const {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}

template<typename T, size_t Size>
bool SpscQueue<T, Size>::full() const {
    size_t next_tail = (tail_.load(std::memory_order_acquire) + 1) & mask_;
    return next_tail == head_.load(std::memory_order_acquire);
}

template<typename T, size_t Size>
void SpscQueue<T, Size>::clear() {
    head_.store(0, std::memory_order_release);
    tail_.store(0, std::memory_order_release);
}

// Explicit template instantiations
template class SpscQueue<int8_t, 16>;
template class SpscQueue<int16_t, 16>;
template class SpscQueue<int32_t, 16>;
template class SpscQueue<int64_t, 16>;
template class SpscQueue<float, 16>;
template class SpscQueue<double, 16>;

} // namespace windmi
