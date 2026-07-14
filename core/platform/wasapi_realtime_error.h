#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace sar::platform {

inline constexpr std::size_t kWasapiRealtimeErrorCapacity = 8;
inline constexpr std::uint16_t kWasapiRealtimeErrorOverflowCode = 0xFFFFU;

struct WasapiRealtimeErrorRecord {
  std::uint16_t code = 0;
  std::uint16_t context = 0;
  std::uint32_t value = 0;
};

[[nodiscard]] std::uint64_t pack_wasapi_realtime_error(
    WasapiRealtimeErrorRecord error) noexcept;
[[nodiscard]] WasapiRealtimeErrorRecord unpack_wasapi_realtime_error(
    std::uint64_t packed) noexcept;

struct WasapiRealtimeErrorBatch {
  std::array<WasapiRealtimeErrorRecord, kWasapiRealtimeErrorCapacity> records{};
  std::uint32_t count = 0;
  std::uint32_t overflow_count = 0;

  void clear() noexcept;
  [[nodiscard]] bool push(WasapiRealtimeErrorRecord error) noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool overflowed() const noexcept;
};

// Publication is single-writer. Snapshots may be taken concurrently by any
// number of readers without allocating or blocking the publisher.
class WasapiRealtimeErrorChannel {
 public:
  WasapiRealtimeErrorChannel() noexcept = default;
  WasapiRealtimeErrorChannel(const WasapiRealtimeErrorChannel&) = delete;
  WasapiRealtimeErrorChannel& operator=(const WasapiRealtimeErrorChannel&) = delete;

  void clear() noexcept;
  void publish(const WasapiRealtimeErrorBatch& batch) noexcept;
  [[nodiscard]] WasapiRealtimeErrorBatch snapshot() const noexcept;

 private:
  std::atomic<std::uint64_t> sequence_ = 0;
  std::array<std::atomic<std::uint64_t>, kWasapiRealtimeErrorCapacity> records_{};
  std::atomic<std::uint64_t> metadata_ = 0;
};

}  // namespace sar::platform
