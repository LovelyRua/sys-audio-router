#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace sar::realtime {

template <typename T>
class SpscRingBuffer {
 public:
  explicit SpscRingBuffer(std::size_t capacity)
      : capacity_(capacity + 1), buffer_(capacity_) {
    if (capacity == 0) {
      throw std::invalid_argument("SpscRingBuffer requires non-zero capacity");
    }
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
    const auto read = read_index_.load(std::memory_order_relaxed);

    if (read == write_index_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    auto value = buffer_[read];
    read_index_.store(increment(read), std::memory_order_release);
    return value;
  }

 private:
  [[nodiscard]] std::size_t increment(std::size_t value) const noexcept {
    return (value + 1) % capacity_;
  }

  std::size_t capacity_;
  std::vector<T> buffer_;
  std::atomic<std::size_t> read_index_ = 0;
  std::atomic<std::size_t> write_index_ = 0;
};

}  // namespace sar::realtime
