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

using Code = WasapiRealtimeErrorCode;
using Context = WasapiRealtimeErrorContext;

[[nodiscard]] constexpr std::uint16_t bits(Context context) noexcept {
  return static_cast<std::uint16_t>(context);
}

[[nodiscard]] Code mapped_code(std::string_view code) noexcept {
  if (code == "stream_not_started") return Code::StreamNotStarted;
  if (code == "wrong_stream_direction") return Code::WrongStreamDirection;
  if (code == "native_stream_unavailable") return Code::NativeStreamUnavailable;
  if (code == "wasapi_event_wait_failed") return Code::WasapiEventWaitFailed;
  if (code == "wasapi_padding_failed") return Code::WasapiPaddingFailed;
  if (code == "wasapi_render_buffer_failed") return Code::WasapiRenderBufferFailed;
  if (code == "wasapi_render_buffer_release_failed") {
    return Code::WasapiRenderBufferReleaseFailed;
  }
  if (code == "wasapi_capture_packet_failed") return Code::WasapiCapturePacketFailed;
  if (code == "capture_buffer_too_small") return Code::CaptureBufferTooSmall;
  if (code == "wasapi_capture_buffer_failed") return Code::WasapiCaptureBufferFailed;
  if (code == "wasapi_capture_buffer_release_failed") {
    return Code::WasapiCaptureBufferReleaseFailed;
  }
  if (code == "sample_channel_mismatch") return Code::SampleChannelMismatch;
  if (code == "sample_conversion_failed") return Code::SampleConversionFailed;
  if (code == "graph_sample_rate_mismatch") return Code::GraphSampleRateMismatch;
  if (code == "graph_buffer_too_small") return Code::GraphBufferTooSmall;
  if (code == "wasapi_duplex_event_wait_failed") {
    return Code::WasapiDuplexEventWaitFailed;
  }
  if (code == "render_committed_too_many_frames") {
    return Code::RenderCommittedTooManyFrames;
  }
  if (code == "capture_resampler_preroll_failed") {
    return Code::CaptureResamplerPrerollFailed;
  }
  if (code == "capture_resampler_preroll_stalled") {
    return Code::CaptureResamplerPrerollStalled;
  }
  if (code == "capture_resampler_failed") return Code::CaptureResamplerFailed;
  return Code::Unclassified;
}

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

WasapiRealtimeErrorRecord map_wasapi_realtime_error(
    std::string_view code,
    std::string_view message,
    bool has_native_hresult,
    std::int32_t native_hresult,
    bool has_native_win32_code,
    std::uint32_t native_win32_code) noexcept {
  std::uint16_t context = 0;
  if (message.find("capture") != std::string_view::npos) {
    context |= bits(Context::Capture);
  } else if (message.find("render") != std::string_view::npos) {
    context |= bits(Context::Render);
  }

  std::uint32_t value = 0;
  if (has_native_hresult) {
    context |= bits(Context::NativeHresult);
    value = static_cast<std::uint32_t>(native_hresult);
  } else if (has_native_win32_code) {
    context |= bits(Context::NativeWin32);
    value = native_win32_code;
  }
  return {static_cast<std::uint16_t>(mapped_code(code)), context, value};
}

const char* wasapi_realtime_error_code(WasapiRealtimeErrorRecord error) noexcept {
  switch (static_cast<Code>(error.code)) {
    case Code::StreamNotStarted: return "stream_not_started";
    case Code::WrongStreamDirection: return "wrong_stream_direction";
    case Code::NativeStreamUnavailable: return "native_stream_unavailable";
    case Code::WasapiEventWaitFailed: return "wasapi_event_wait_failed";
    case Code::WasapiPaddingFailed: return "wasapi_padding_failed";
    case Code::WasapiRenderBufferFailed: return "wasapi_render_buffer_failed";
    case Code::WasapiRenderBufferReleaseFailed:
      return "wasapi_render_buffer_release_failed";
    case Code::WasapiCapturePacketFailed: return "wasapi_capture_packet_failed";
    case Code::CaptureBufferTooSmall: return "capture_buffer_too_small";
    case Code::WasapiCaptureBufferFailed: return "wasapi_capture_buffer_failed";
    case Code::WasapiCaptureBufferReleaseFailed:
      return "wasapi_capture_buffer_release_failed";
    case Code::SampleChannelMismatch: return "sample_channel_mismatch";
    case Code::SampleConversionFailed: return "sample_conversion_failed";
    case Code::GraphSampleRateMismatch: return "graph_sample_rate_mismatch";
    case Code::GraphBufferTooSmall: return "graph_buffer_too_small";
    case Code::WasapiDuplexEventWaitFailed: return "wasapi_duplex_event_wait_failed";
    case Code::RenderCommittedTooManyFrames:
      return "render_committed_too_many_frames";
    case Code::CaptureResamplerPrerollFailed:
      return "capture_resampler_preroll_failed";
    case Code::CaptureResamplerPrerollStalled:
      return "capture_resampler_preroll_stalled";
    case Code::CaptureResamplerFailed: return "capture_resampler_failed";
    case Code::Unclassified: return "unclassified_realtime_error";
  }
  if (error.code == kWasapiRealtimeErrorOverflowCode) {
    return "realtime_error_overflow";
  }
  return "unclassified_realtime_error";
}

const char* wasapi_realtime_error_message(WasapiRealtimeErrorRecord error) noexcept {
  switch (static_cast<Code>(error.code)) {
    case Code::StreamNotStarted: return "WASAPI processing requires a started stream.";
    case Code::WrongStreamDirection: return "WASAPI stream direction is invalid for processing.";
    case Code::NativeStreamUnavailable: return "WASAPI processing requires a native opened stream.";
    case Code::WasapiEventWaitFailed: return "WASAPI event wait failed.";
    case Code::WasapiPaddingFailed: return "WASAPI render padding query failed.";
    case Code::WasapiRenderBufferFailed: return "WASAPI render buffer acquisition failed.";
    case Code::WasapiRenderBufferReleaseFailed: return "WASAPI render buffer release failed.";
    case Code::WasapiCapturePacketFailed: return "WASAPI capture packet query failed.";
    case Code::CaptureBufferTooSmall: return "Capture destination buffer cannot hold the packet.";
    case Code::WasapiCaptureBufferFailed: return "WASAPI capture buffer acquisition failed.";
    case Code::WasapiCaptureBufferReleaseFailed: return "WASAPI capture buffer release failed.";
    case Code::SampleChannelMismatch: return "Audio buffer channel count does not match the stream.";
    case Code::SampleConversionFailed: return "Sample conversion failed.";
    case Code::GraphSampleRateMismatch: return "Graph sample rate must match the WASAPI stream sample rate.";
    case Code::GraphBufferTooSmall: return "Graph scratch buffers must cover the runner buffer shapes.";
    case Code::WasapiDuplexEventWaitFailed: return "WASAPI duplex event wait failed.";
    case Code::RenderCommittedTooManyFrames: return "WASAPI render stream committed more frames than were staged.";
    case Code::CaptureResamplerPrerollFailed: return "Adaptive capture resampler recovery pre-roll failed.";
    case Code::CaptureResamplerPrerollStalled: return "Adaptive capture resampler recovery pre-roll made no progress.";
    case Code::CaptureResamplerFailed: return "Adaptive capture resampling failed in the buffered duplex path.";
    case Code::Unclassified: return "An unclassified realtime WASAPI error occurred.";
  }
  if (error.code == kWasapiRealtimeErrorOverflowCode) {
    return "Additional realtime WASAPI errors were omitted.";
  }
  return "An unclassified realtime WASAPI error occurred.";
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
