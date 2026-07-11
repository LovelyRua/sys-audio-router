#pragma once

#include <atomic>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace sar::realtime {

template <typename T>
class SpscRingBuffer {
 public:
  explicit SpscRingBuffer(std::size_t capacity)
      : capacity_(checked_capacity(capacity)), buffer_(capacity_) {
    static_assert(std::is_default_constructible_v<T>);
    static_assert(std::is_copy_constructible_v<T>);
    static_assert(std::is_copy_assignable_v<T>);
  }

  [[nodiscard]] std::size_t capacity() const noexcept {
    return capacity_ - 1;
  }

  [[nodiscard]] bool empty() const noexcept {
    return read_index_.load(std::memory_order_acquire) ==
           write_index_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool full() const noexcept {
    const auto write = write_index_.load(std::memory_order_relaxed);
    const auto next = increment(write);
    return next == read_index_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool push(const T& value) noexcept {
    const auto write = write_index_.load(std::memory_order_relaxed);
    const auto next = increment(write);

    if (next == read_index_.load(std::memory_order_acquire)) {
      return false;
    }

    buffer_[write] = value;
    write_index_.store(next, std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::optional<T> pop() noexcept {
    T value{};
    if (!try_pop(value)) {
      return std::nullopt;
    }
    return value;
  }

  // Use this form on a realtime consumer to avoid constructing an optional
  // result object for every successful dequeue.
  [[nodiscard]] bool try_pop(T& value) noexcept {
    const auto read = read_index_.load(std::memory_order_relaxed);
    if (read == write_index_.load(std::memory_order_acquire)) {
      return false;
    }

    value = buffer_[read];
    read_index_.store(increment(read), std::memory_order_release);
    return true;
  }

 private:
  [[nodiscard]] static std::size_t checked_capacity(std::size_t capacity) {
    if (capacity == 0 || capacity == std::numeric_limits<std::size_t>::max()) {
      throw std::invalid_argument("SpscRingBuffer capacity must be non-zero and finite");
    }
    return capacity + 1;
  }

  [[nodiscard]] std::size_t increment(std::size_t value) const noexcept {
    return (value + 1) % capacity_;
  }

  std::size_t capacity_;
  std::vector<T> buffer_;
  std::atomic<std::size_t> read_index_ = 0;
  std::atomic<std::size_t> write_index_ = 0;
};

}  // namespace sar::realtime
