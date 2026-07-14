#include "core/platform/wasapi_realtime_error.h"

#include <algorithm>
#include <limits>
#include <type_traits>

namespace sar::platform {
namespace {

static_assert(std::is_trivially_copyable_v<WasapiRealtimeErrorRecord>);
static_assert(std::is_trivially_copyable_v<WasapiRealtimeErrorBatch>);
static_assert(sizeof(WasapiRealtimeErrorRecord) == sizeof(std::uint64_t));
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

constexpr std::uint64_t kFieldMask16 = 0xFFFFULL;
constexpr std::uint64_t kFieldMask32 = 0xFFFFFFFFULL;
constexpr unsigned int kContextShift = 16;
constexpr unsigned int kValueShift = 32;

[[nodiscard]] constexpr std::uint32_t saturated_increment(std::uint32_t value) noexcept {
  return value == std::numeric_limits<std::uint32_t>::max() ? value : value + 1;
}

[[nodiscard]] constexpr WasapiRealtimeErrorRecord overflow_record(
    std::uint32_t overflow_count) noexcept {
  return {kWasapiRealtimeErrorOverflowCode, 0, overflow_count};
}

[[nodiscard]] constexpr std::uint64_t pack_metadata(std::uint32_t count,
                                                    std::uint32_t overflow_count) noexcept {
  return static_cast<std::uint64_t>(count) |
         (static_cast<std::uint64_t>(overflow_count) << kValueShift);
}

}  // namespace

std::uint64_t pack_wasapi_realtime_error(WasapiRealtimeErrorRecord error) noexcept {
  return static_cast<std::uint64_t>(error.code) |
         (static_cast<std::uint64_t>(error.context) << kContextShift) |
         (static_cast<std::uint64_t>(error.value) << kValueShift);
}

WasapiRealtimeErrorRecord unpack_wasapi_realtime_error(std::uint64_t packed) noexcept {
  return {
      static_cast<std::uint16_t>(packed & kFieldMask16),
      static_cast<std::uint16_t>((packed >> kContextShift) & kFieldMask16),
      static_cast<std::uint32_t>((packed >> kValueShift) & kFieldMask32),
  };
}

void WasapiRealtimeErrorBatch::clear() noexcept {
  records = {};
  count = 0;
  overflow_count = 0;
}

bool WasapiRealtimeErrorBatch::push(WasapiRealtimeErrorRecord error) noexcept {
  if (overflow_count != 0) {
    overflow_count = saturated_increment(overflow_count);
    records.back() = overflow_record(overflow_count);
    return false;
  }

  if (count < kWasapiRealtimeErrorCapacity) {
    records[count] = error;
    ++count;
    return true;
  }

  // Slot eight becomes the marker. It accounts for both the displaced eighth
  // record and the new record that triggered overflow.
  overflow_count = 2;
  records.back() = overflow_record(overflow_count);
  return false;
}

bool WasapiRealtimeErrorBatch::empty() const noexcept {
  return count == 0;
}

std::size_t WasapiRealtimeErrorBatch::size() const noexcept {
  return std::min<std::size_t>(count, kWasapiRealtimeErrorCapacity);
}

bool WasapiRealtimeErrorBatch::overflowed() const noexcept {
  return overflow_count != 0;
}

void WasapiRealtimeErrorChannel::clear() noexcept {
  publish({});
}

void WasapiRealtimeErrorChannel::publish(const WasapiRealtimeErrorBatch& batch) noexcept {
  const auto write_sequence = sequence_.load(std::memory_order_relaxed) + 1;
  sequence_.store(write_sequence, std::memory_order_seq_cst);

  const auto count = std::min<std::uint32_t>(
      batch.count, static_cast<std::uint32_t>(kWasapiRealtimeErrorCapacity));
  for (std::size_t index = 0; index < kWasapiRealtimeErrorCapacity; ++index) {
    records_[index].store(pack_wasapi_realtime_error(batch.records[index]),
                          std::memory_order_seq_cst);
  }
  metadata_.store(pack_metadata(count, batch.overflow_count), std::memory_order_seq_cst);

  sequence_.store(write_sequence + 1, std::memory_order_seq_cst);
}

WasapiRealtimeErrorBatch WasapiRealtimeErrorChannel::snapshot() const noexcept {
  WasapiRealtimeErrorBatch result{};
  for (;;) {
    const auto before = sequence_.load(std::memory_order_seq_cst);
    if ((before & 1U) != 0) {
      continue;
    }

    for (std::size_t index = 0; index < kWasapiRealtimeErrorCapacity; ++index) {
      result.records[index] =
          unpack_wasapi_realtime_error(records_[index].load(std::memory_order_seq_cst));
    }
    const auto metadata = metadata_.load(std::memory_order_seq_cst);

    const auto after = sequence_.load(std::memory_order_seq_cst);
    if (before == after) {
      result.count = static_cast<std::uint32_t>(metadata & kFieldMask32);
      result.overflow_count = static_cast<std::uint32_t>(metadata >> kValueShift);
      return result;
    }
  }
}

}  // namespace sar::platform
