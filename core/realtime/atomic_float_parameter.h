#pragma once

#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>

namespace sar::realtime {

// Publishes finite control values to the realtime thread without allocation or locks.
class AtomicFloatParameter {
 public:
  explicit AtomicFloatParameter(float initial_value = 0.0F) noexcept
      : bits_(to_bits(std::isfinite(initial_value) ? initial_value : 0.0F)) {}

  [[nodiscard]] bool set(float value) noexcept {
    if (!std::isfinite(value)) {
      return false;
    }
    bits_.store(to_bits(value), std::memory_order_relaxed);
    return true;
  }

  [[nodiscard]] float get() const noexcept {
    return from_bits(bits_.load(std::memory_order_relaxed));
  }

 private:
  static constexpr std::uint32_t to_bits(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
  }

  static constexpr float from_bits(std::uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
  }

  static_assert(std::atomic_uint32_t::is_always_lock_free,
                "AtomicFloatParameter requires a lock-free 32-bit atomic.");

  std::atomic_uint32_t bits_;
};

}  // namespace sar::realtime
