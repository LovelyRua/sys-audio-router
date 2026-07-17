#include "core/service/windows_wasapi_engine_runtime.h"

#include <utility>

namespace sar::service {

namespace {

std::vector<EngineAudioRuntimeError> convert_errors(
    const std::vector<platform::WasapiRealtimeWorkerError>& errors) {
  std::vector<EngineAudioRuntimeError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back(
        {error.code, error.message, error.native_hresult, error.native_win32_code});
  }
  return converted;
}

}  // namespace

WindowsWasapiEngineRuntime::~WindowsWasapiEngineRuntime() {
  stop();
}

WindowsWasapiEngineRuntimeOpenResult
WindowsWasapiEngineRuntime::open_default_render(
    std::shared_ptr<graph::Graph> graph) {
  if (!graph) {
    return WindowsWasapiEngineRuntimeOpenResult::failure({
        {"null_runtime_graph", "WASAPI engine runtime requires a graph."},
    });
  }

  auto runtime = std::unique_ptr<WindowsWasapiEngineRuntime>(
      new WindowsWasapiEngineRuntime(std::move(graph)));
  auto loop = platform::open_default_wasapi_render_loop(
      *runtime->graph_, runtime->realtime_diagnostics_);
  if (!loop.ok()) {
    return WindowsWasapiEngineRuntimeOpenResult::failure(
        convert_errors(loop.errors()));
  }
  runtime->render_loop_ = loop.take_loop();
  return WindowsWasapiEngineRuntimeOpenResult::success(std::move(runtime));
}

EngineAudioRuntimeResult WindowsWasapiEngineRuntime::start(
    std::uint32_t timeout_ms) {
  if (!render_loop_) {
    return EngineAudioRuntimeResult::failure({
        {"wasapi_render_loop_not_open", "WASAPI render loop is not open."},
    });
  }
  const auto result = render_loop_->start(timeout_ms);
  if (!result.ok()) {
    return EngineAudioRuntimeResult::failure(convert_errors(result.errors()));
  }
  return EngineAudioRuntimeResult::success();
}

void WindowsWasapiEngineRuntime::stop() noexcept {
  if (render_loop_) {
    render_loop_->stop();
  }
}

bool WindowsWasapiEngineRuntime::running() const noexcept {
  return render_loop_ && render_loop_->running();
}

std::uint64_t WindowsWasapiEngineRuntime::graph_version() const noexcept {
  return graph_ ? graph_->version() : 0;
}

diagnostics::EngineDiagnostics WindowsWasapiEngineRuntime::diagnostics() const {
  diagnostics::EngineDiagnostics result;
  if (!render_loop_) {
    return result;
  }

  const auto snapshot = render_loop_->summary().runtime;
  result.graph_version = graph_->version();
  result.processed_blocks = snapshot.graph_processed_cycles;
  result.xrun_count = snapshot.xrun_count;
  result.capture_fifo_fill_frames = snapshot.capture_fifo_fill_frames;
  result.render_fifo_fill_frames = snapshot.render_fifo_fill_frames;
  result.capture_fifo_overflow_cycles = snapshot.capture_fifo_overflow_cycles;
  result.capture_fifo_overflow_frames = snapshot.capture_fifo_overflow_frames;
  result.render_fifo_overflow_cycles = snapshot.render_fifo_overflow_cycles;
  result.render_fifo_overflow_frames = snapshot.render_fifo_overflow_frames;
  result.render_fifo_underflow_cycles = snapshot.render_fifo_underflow_cycles;
  result.render_fifo_underflow_frames = snapshot.render_fifo_underflow_frames;
  result.last_callback_seconds =
      static_cast<double>(snapshot.last_callback_nanoseconds) / 1'000'000'000.0;
  result.peak_callback_seconds =
      static_cast<double>(snapshot.peak_callback_nanoseconds) / 1'000'000'000.0;
  return result;
}

platform::WasapiRenderLoopSummary WindowsWasapiEngineRuntime::summary() const {
  return render_loop_ ? render_loop_->summary()
                      : platform::WasapiRenderLoopSummary{};
}

WindowsWasapiEngineRuntime::WindowsWasapiEngineRuntime(
    std::shared_ptr<graph::Graph> graph) noexcept
    : graph_(std::move(graph)) {}

WindowsWasapiEngineRuntimeOpenResult
WindowsWasapiEngineRuntimeOpenResult::success(
    std::unique_ptr<WindowsWasapiEngineRuntime> runtime) {
  return {std::move(runtime), {}};
}

WindowsWasapiEngineRuntimeOpenResult
WindowsWasapiEngineRuntimeOpenResult::failure(
    std::vector<EngineAudioRuntimeError> errors) {
  return {nullptr, std::move(errors)};
}

bool WindowsWasapiEngineRuntimeOpenResult::ok() const noexcept {
  return runtime_ != nullptr && errors_.empty();
}

std::unique_ptr<WindowsWasapiEngineRuntime>
WindowsWasapiEngineRuntimeOpenResult::take_runtime() noexcept {
  return std::move(runtime_);
}

const std::vector<EngineAudioRuntimeError>&
WindowsWasapiEngineRuntimeOpenResult::errors() const noexcept {
  return errors_;
}

WindowsWasapiEngineRuntimeOpenResult::WindowsWasapiEngineRuntimeOpenResult(
    std::unique_ptr<WindowsWasapiEngineRuntime> runtime,
    std::vector<EngineAudioRuntimeError> errors) noexcept
    : runtime_(std::move(runtime)), errors_(std::move(errors)) {}

}  // namespace sar::service
