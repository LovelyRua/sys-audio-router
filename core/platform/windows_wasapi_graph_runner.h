#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_stream.h"
#include "core/realtime/adaptive_resampler.h"
#include "core/realtime/audio_buffer.h"
#include "core/realtime/fifo_waterline_controller.h"
#include "core/realtime/planar_audio_fifo.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sar::platform {

struct WasapiGraphRunnerStats {
  std::uint32_t captured_frames = 0;
  std::uint32_t rendered_frames = 0;
  std::uint32_t capture_partial_frames = 0;
  std::uint32_t render_partial_frames = 0;
  std::uint32_t capture_silent_frames = 0;
  std::uint32_t capture_resampler_input_frames = 0;
  std::uint32_t capture_resampler_output_frames = 0;
  double capture_rate_correction_ppm = 0.0;
  double capture_resampler_ratio = 1.0;
  bool capture_rate_adapter_active = false;
  bool graph_processed = false;
  bool capture_stream_idle = false;
  bool render_stream_idle = false;
  bool capture_wait_timed_out = false;
  bool render_wait_timed_out = false;
  bool cancelled = false;
  bool capture_partial = false;
  bool render_partial = false;
  bool capture_silent = false;
  bool capture_data_discontinuity = false;
  bool capture_timestamp_error = false;
};

class WasapiGraphRunnerResult {
 public:
  static WasapiGraphRunnerResult success(WasapiGraphRunnerStats stats);
  static WasapiGraphRunnerResult failure(std::vector<WasapiStreamError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const WasapiGraphRunnerStats& stats() const noexcept;
  [[nodiscard]] const std::vector<WasapiStreamError>& errors() const noexcept;

 private:
  WasapiGraphRunnerResult(WasapiGraphRunnerStats stats,
                          std::vector<WasapiStreamError> errors);

  WasapiGraphRunnerStats stats_;
  std::vector<WasapiStreamError> errors_;
};

class WindowsWasapiGraphRunner {
 public:
  WindowsWasapiGraphRunner(WasapiStreamIo* capture_stream,
                           WasapiStreamIo* render_stream,
                           std::size_t channels,
                           std::size_t frames);
  WindowsWasapiGraphRunner(WasapiStreamIo* capture_stream,
                           WasapiStreamIo* render_stream,
                           std::size_t capture_channels,
                           std::size_t capture_frames,
                           std::size_t render_channels,
                           std::size_t render_frames);
  WindowsWasapiGraphRunner(WasapiStreamIo* capture_stream,
                           WasapiStreamIo* render_stream,
                           std::size_t input_channels,
                           std::size_t output_channels,
                           std::size_t graph_block_frames,
                           std::size_t capture_packet_capacity_frames,
                           std::size_t render_packet_capacity_frames,
                           std::size_t fifo_capacity_frames,
                           bool prime_render_silence = false,
                           bool adapt_capture_rate = false);

  [[nodiscard]] realtime::AudioBuffer& input_buffer() noexcept;
  [[nodiscard]] const realtime::AudioBuffer& input_buffer() const noexcept;
  [[nodiscard]] realtime::AudioBuffer& output_buffer() noexcept;
  [[nodiscard]] const realtime::AudioBuffer& output_buffer() const noexcept;

  [[nodiscard]] WasapiGraphRunnerResult start_streams() noexcept;
  [[nodiscard]] WasapiGraphRunnerResult stop_streams() noexcept;
  void request_stop() noexcept;
  [[nodiscard]] WasapiGraphRunnerResult process_once(
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      std::uint32_t timeout_ms) noexcept;

 private:
  struct BufferedPath {
    BufferedPath(std::size_t channels,
                 std::size_t packet_frames,
                 std::size_t fifo_frames);

    realtime::AudioBuffer packet;
    realtime::PlanarAudioFifo fifo;
  };

  struct CaptureRateAdapter {
    CaptureRateAdapter(std::size_t channels,
                       std::size_t graph_frames,
                       std::size_t capture_packet_frames,
                       std::size_t fifo_frames);

    realtime::AdaptiveResampler resampler;
    realtime::FifoWaterlineController controller;
    realtime::AudioBuffer source_planar;
    std::vector<float> source_interleaved;
    std::vector<float> output_interleaved;
    std::size_t target_fill_frames = 0;
    std::size_t output_frames_ready = 0;
    double ratio = 1.0;
    bool ratio_set_for_block = false;
    bool primed = false;
  };

  [[nodiscard]] WasapiGraphRunnerResult process_buffered_once(
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      std::uint32_t timeout_ms) noexcept;

  WasapiStreamIo* capture_stream_ = nullptr;
  WasapiStreamIo* render_stream_ = nullptr;
  realtime::AudioBuffer input_;
  realtime::AudioBuffer output_;
  std::size_t graph_block_frames_ = 0;
  bool render_master_ = false;
  std::optional<BufferedPath> capture_path_;
  std::optional<BufferedPath> render_path_;
  std::optional<CaptureRateAdapter> capture_rate_adapter_;
};

}  // namespace sar::platform
