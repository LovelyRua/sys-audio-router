#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sar::platform {

inline constexpr std::size_t kWasapiRealtimeErrorCapacity = 8;
inline constexpr std::uint16_t kWasapiRealtimeErrorOverflowCode = 0xFFFFU;

enum class WasapiRealtimeErrorCode : std::uint16_t {
  Unclassified = 1,
  StreamNotStarted,
  WrongStreamDirection,
  NativeStreamUnavailable,
  WasapiEventWaitFailed,
  WasapiPaddingFailed,
  WasapiRenderBufferFailed,
  WasapiRenderBufferReleaseFailed,
  WasapiCapturePacketFailed,
  CaptureBufferTooSmall,
  WasapiCaptureBufferFailed,
  WasapiCaptureBufferReleaseFailed,
  SampleChannelMismatch,
  SampleConversionFailed,
  GraphSampleRateMismatch,
  GraphBufferTooSmall,
  WasapiDuplexEventWaitFailed,
  RenderCommittedTooManyFrames,
  CaptureResamplerPrerollFailed,
  CaptureResamplerPrerollStalled,
  CaptureResamplerFailed,
};

enum class WasapiRealtimeErrorContext : std::uint16_t {
  None = 0,
  Capture = 1,
  Render = 2,
  NativeHresult = 4,
  NativeWin32 = 8,
};

struct WasapiRealtimeErrorRecord {
  std::uint16_t code = 0;
  std::uint16_t context = 0;
  std::uint32_t value = 0;
};

[[nodiscard]] std::uint64_t pack_wasapi_realtime_error(
    WasapiRealtimeErrorRecord error) noexcept;
[[nodiscard]] WasapiRealtimeErrorRecord unpack_wasapi_realtime_error(
    std::uint64_t packed) noexcept;
[[nodiscard]] WasapiRealtimeErrorRecord map_wasapi_realtime_error(
    std::string_view code,
    std::string_view message,
    bool has_native_hresult = false,
    std::int32_t native_hresult = 0,
    bool has_native_win32_code = false,
    std::uint32_t native_win32_code = 0) noexcept;
[[nodiscard]] const char* wasapi_realtime_error_code(
    WasapiRealtimeErrorRecord error) noexcept;
[[nodiscard]] const char* wasapi_realtime_error_message(
    WasapiRealtimeErrorRecord error) noexcept;

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
