#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_stream.h"
#include "core/realtime/audio_buffer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sar::platform {

struct WasapiGraphRunnerStats {
  std::uint32_t captured_frames = 0;
  std::uint32_t rendered_frames = 0;
  bool graph_processed = false;
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
  WindowsWasapiGraphRunner(WindowsWasapiStream* capture_stream,
                           WindowsWasapiStream* render_stream,
                           std::size_t channels,
                           std::size_t frames);

  [[nodiscard]] realtime::AudioBuffer& input_buffer() noexcept;
  [[nodiscard]] const realtime::AudioBuffer& input_buffer() const noexcept;
  [[nodiscard]] realtime::AudioBuffer& output_buffer() noexcept;
  [[nodiscard]] const realtime::AudioBuffer& output_buffer() const noexcept;

  [[nodiscard]] WasapiGraphRunnerResult process_once(
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      std::uint32_t timeout_ms) noexcept;

 private:
  WindowsWasapiStream* capture_stream_ = nullptr;
  WindowsWasapiStream* render_stream_ = nullptr;
  realtime::AudioBuffer input_;
  realtime::AudioBuffer output_;
};

}  // namespace sar::platform
