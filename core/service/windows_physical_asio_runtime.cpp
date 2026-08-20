#include "core/service/windows_physical_asio_runtime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace sar::service {
namespace {

bool compatible_sample_rate(double negotiated, std::uint32_t graph_rate) noexcept {
  return std::isfinite(negotiated) &&
         std::fabs(negotiated - static_cast<double>(graph_rate)) < 0.5;
}

}  // namespace

WindowsPhysicalAsioRuntime::WindowsPhysicalAsioRuntime(
    std::shared_ptr<graph::Graph> graph) noexcept
    : graph_(std::move(graph)) {}

WindowsPhysicalAsioRuntime::~WindowsPhysicalAsioRuntime() {
  static_cast<void>(stop());
  control_open_.host.reset();
}

WindowsPhysicalAsioRuntimeOpenResult WindowsPhysicalAsioRuntime::open(
    std::shared_ptr<graph::Graph> graph,
    platform::WindowsAsioControlOpenRequest request,
    platform::WindowsAsioDriverActivator& activator,
    platform::WindowsAsioDriverNegotiator& negotiator) noexcept {
  if (!graph || request.driver.clsid.empty()) {
    return {{}, WindowsPhysicalAsioRuntimeError::InvalidRequest,
            platform::WindowsAsioControlOpenError::InvalidRequest};
  }

  auto runtime = std::unique_ptr<WindowsPhysicalAsioRuntime>(
      new (std::nothrow) WindowsPhysicalAsioRuntime(std::move(graph)));
  if (!runtime) {
    return {{}, WindowsPhysicalAsioRuntimeError::ResourceExhausted,
            platform::WindowsAsioControlOpenError::None};
  }

  request.graph_process = &WindowsPhysicalAsioRuntime::graph_callback;
  request.graph_context = runtime.get();
  request.host_events = &runtime->host_events_;
  runtime->control_open_ = platform::open_windows_asio_control(
      request, activator, negotiator);
  if (!runtime->control_open_.ok()) {
    const auto control_error = runtime->control_open_.error;
    return {{}, WindowsPhysicalAsioRuntimeError::ControlOpenFailed,
            control_error};
  }

  const auto& config = runtime->control_open_.config;
  const auto graph_channels = std::max(config.inputs.size(),
                                       config.outputs.size());
  if (graph_channels == 0 ||
      graph_channels != runtime->graph_->channels() ||
      config.frames_per_block != runtime->graph_->frames() ||
      !compatible_sample_rate(config.sample_rate,
                              runtime->graph_->sample_rate())) {
    return {{}, WindowsPhysicalAsioRuntimeError::GraphConfigurationMismatch,
            platform::WindowsAsioControlOpenError::None};
  }

  try {
    runtime->graph_input_ = std::make_unique<realtime::AudioBuffer>(
        graph_channels, config.frames_per_block);
    runtime->graph_output_ = std::make_unique<realtime::AudioBuffer>(
        graph_channels, config.frames_per_block);
  } catch (...) {
    return {{}, WindowsPhysicalAsioRuntimeError::ResourceExhausted,
            platform::WindowsAsioControlOpenError::None};
  }

  return {std::move(runtime), WindowsPhysicalAsioRuntimeError::None,
          platform::WindowsAsioControlOpenError::None};
}

WindowsPhysicalAsioRuntimeError WindowsPhysicalAsioRuntime::start() noexcept {
  if (state_.load(std::memory_order_acquire) ==
      WindowsPhysicalAsioRuntimeState::Running) {
    return WindowsPhysicalAsioRuntimeError::None;
  }
  state_.store(WindowsPhysicalAsioRuntimeState::Running,
               std::memory_order_release);
  const auto result = control_open_.host->start();
  if (!result.ok()) {
    vendor_host_error_ = result.error;
    last_error_ = WindowsPhysicalAsioRuntimeError::StartFailed;
    state_.store(WindowsPhysicalAsioRuntimeState::Ready,
                 std::memory_order_release);
    return last_error_;
  }
  vendor_host_error_ = platform::WindowsAsioVendorHostError::None;
  last_error_ = WindowsPhysicalAsioRuntimeError::None;
  return WindowsPhysicalAsioRuntimeError::None;
}

WindowsPhysicalAsioRuntimeError WindowsPhysicalAsioRuntime::stop() noexcept {
  if (!control_open_.host ||
      state_.load(std::memory_order_acquire) !=
          WindowsPhysicalAsioRuntimeState::Running) {
    return WindowsPhysicalAsioRuntimeError::None;
  }
  const auto result = control_open_.host->stop();
  if (!result.ok()) {
    vendor_host_error_ = result.error;
    last_error_ = WindowsPhysicalAsioRuntimeError::StopFailed;
    return last_error_;
  }
  vendor_host_error_ = platform::WindowsAsioVendorHostError::None;
  last_error_ = WindowsPhysicalAsioRuntimeError::None;
  state_.store(WindowsPhysicalAsioRuntimeState::Stopped,
               std::memory_order_release);
  return WindowsPhysicalAsioRuntimeError::None;
}

WindowsPhysicalAsioRuntimeSummary WindowsPhysicalAsioRuntime::summary() const
    noexcept {
  WindowsPhysicalAsioRuntimeSummary result;
  result.state = state_.load(std::memory_order_acquire);
  result.last_error = last_error_.load(std::memory_order_acquire);
  result.control_open_error = control_open_.error;
  result.vendor_host_error = vendor_host_error_.load(std::memory_order_acquire);
  result.sample_rate = control_open_.config.sample_rate;
  result.frames_per_block = control_open_.config.frames_per_block;
  result.input_channels = static_cast<std::uint32_t>(
      control_open_.config.inputs.size());
  result.output_channels = static_cast<std::uint32_t>(
      control_open_.config.outputs.size());
  result.rejected_callbacks =
      rejected_callbacks_.load(std::memory_order_relaxed);
  result.diagnostics_available =
      result.state != WindowsPhysicalAsioRuntimeState::Running;
  if (result.diagnostics_available) {
    result.diagnostics = diagnostics_;
  }
  return result;
}

platform::WindowsAsioHostEventSnapshot
WindowsPhysicalAsioRuntime::drain_host_events() noexcept {
  return host_events_.drain();
}

bool WindowsPhysicalAsioRuntime::graph_callback(
    void* context, const realtime::AudioBuffer& input,
    realtime::AudioBuffer& output) noexcept {
  if (!context) {
    return false;
  }
  return static_cast<WindowsPhysicalAsioRuntime*>(context)->process_graph(
      input, output);
}

bool WindowsPhysicalAsioRuntime::process_graph(
    const realtime::AudioBuffer& input,
    realtime::AudioBuffer& output) noexcept {
  const auto& config = control_open_.config;
  if (!graph_ || !graph_input_ || !graph_output_ ||
      !compatible_sample_rate(config.sample_rate, graph_->sample_rate()) ||
      input.frames() != config.frames_per_block ||
      output.frames() != config.frames_per_block ||
      input.channels() != config.inputs.size() ||
      output.channels() != config.outputs.size() ||
      graph_input_->frames() != config.frames_per_block ||
      graph_output_->frames() != config.frames_per_block ||
      graph_input_->channels() != graph_->channels() ||
      graph_output_->channels() != graph_->channels()) {
    rejected_callbacks_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  graph_input_->copy_from(input);
  graph_output_->clear();
  graph_->process(*graph_input_, *graph_output_, diagnostics_);
  output.copy_from(*graph_output_);
  return true;
}

const char* windows_physical_asio_runtime_error_name(
    WindowsPhysicalAsioRuntimeError error) noexcept {
  switch (error) {
    case WindowsPhysicalAsioRuntimeError::None: return "none";
    case WindowsPhysicalAsioRuntimeError::InvalidRequest: return "invalid_request";
    case WindowsPhysicalAsioRuntimeError::ControlOpenFailed: return "control_open_failed";
    case WindowsPhysicalAsioRuntimeError::GraphConfigurationMismatch:
      return "graph_configuration_mismatch";
    case WindowsPhysicalAsioRuntimeError::ResourceExhausted: return "resource_exhausted";
    case WindowsPhysicalAsioRuntimeError::StartFailed: return "start_failed";
    case WindowsPhysicalAsioRuntimeError::StopFailed: return "stop_failed";
  }
  return "unknown";
}

}  // namespace sar::service
