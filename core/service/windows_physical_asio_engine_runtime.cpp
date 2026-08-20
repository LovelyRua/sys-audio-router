#include "core/service/windows_physical_asio_engine_runtime.h"

#include "core/service/windows_physical_asio_runtime.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace sar::service {
namespace {

EngineAudioRuntimeBuildResult failure(std::string code, std::string message) {
  return EngineAudioRuntimeBuildResult::failure(
      {{std::move(code), std::move(message)}});
}

bool is_complete_contiguous(const std::vector<std::uint32_t>& selected,
                            std::uint32_t available) noexcept {
  if (selected.size() != available) return false;
  for (std::uint32_t index = 0; index < available; ++index) {
    if (selected[index] != index) return false;
  }
  return true;
}

class PhysicalAsioEngineRuntime final : public EngineAudioRuntime {
 public:
  PhysicalAsioEngineRuntime(std::unique_ptr<WindowsPhysicalAsioRuntime> runtime,
                            std::uint64_t graph_version) noexcept
      : runtime_(std::move(runtime)), graph_version_(graph_version) {}

  EngineAudioRuntimeResult start(std::uint32_t) override {
    const auto error = runtime_->start();
    if (error == WindowsPhysicalAsioRuntimeError::None) {
      return EngineAudioRuntimeResult::success();
    }
    return EngineAudioRuntimeResult::failure({{
        windows_physical_asio_runtime_error_name(error),
        "Physical ASIO driver could not start.",
    }});
  }

  void stop() noexcept override { static_cast<void>(runtime_->stop()); }

  bool running() const noexcept override {
    return runtime_->summary().state == WindowsPhysicalAsioRuntimeState::Running;
  }

  std::uint64_t graph_version() const noexcept override {
    return graph_version_;
  }

  diagnostics::EngineDiagnostics diagnostics() const override {
    return runtime_->summary().diagnostics;
  }

 private:
  std::unique_ptr<WindowsPhysicalAsioRuntime> runtime_;
  std::uint64_t graph_version_ = 0;
};

}  // namespace

EngineAudioRuntimeBuildResult open_windows_physical_asio_engine_runtime(
    const control::AudioRuntimeConfiguration& configuration,
    std::shared_ptr<graph::Graph> graph,
    WindowsPhysicalAsioProbe probe,
    platform::WindowsAsioDriverActivator& activator,
    platform::WindowsAsioDriverNegotiator& negotiator) {
  if (configuration.mode != control::AudioRuntimeMode::PhysicalAsio ||
      !graph || !probe) {
    return failure("physical_asio_invalid_request",
                   "Physical ASIO runtime configuration is incomplete.");
  }
  const auto probed = probe(configuration.physical_asio_driver_clsid);
  if (!probed.ok()) {
    return failure("physical_asio_driver_probe_failed",
                   "Physical ASIO driver could not be inspected.");
  }
  const auto& driver = probed.probe();
  if (!is_complete_contiguous(configuration.physical_asio_input_channels,
                              driver.input_channels) ||
      !is_complete_contiguous(configuration.physical_asio_output_channels,
                              driver.output_channels)) {
    return failure(
        "physical_asio_channel_subset_not_implemented",
        "This alpha requires selecting every driver channel in native order; "
        "sparse Physical ASIO channel mapping is not implemented yet.");
  }
  const auto required_channels =
      std::max(driver.input_channels, driver.output_channels);
  if (required_channels == 0 || graph->channels() != required_channels ||
      graph->frames() != configuration.physical_asio_block_frames ||
      graph->sample_rate() != configuration.physical_asio_sample_rate) {
    return failure(
        "physical_asio_graph_shape_mismatch",
        "Preset graph channels, block size, and sample rate must match the "
        "selected Physical ASIO driver configuration.");
  }

  platform::WindowsAsioControlOpenRequest request;
  request.driver = driver;
  request.sample_rate = configuration.physical_asio_sample_rate;
  request.preferred_block_frames = configuration.physical_asio_block_frames;
  auto opened = WindowsPhysicalAsioRuntime::open(
      graph, std::move(request), activator, negotiator);
  if (!opened.ok()) {
    return failure(
        std::string("physical_asio_") +
            windows_physical_asio_runtime_error_name(opened.error),
        std::string("Physical ASIO runtime open failed: ") +
            platform::windows_asio_control_open_error_name(
                opened.control_open_error));
  }
  const auto version = graph->version();
  return EngineAudioRuntimeBuildResult::success(
      std::make_unique<PhysicalAsioEngineRuntime>(std::move(opened.runtime),
                                                  version));
}

EngineAudioRuntimeBuildResult open_windows_physical_asio_engine_runtime(
    const control::AudioRuntimeConfiguration& configuration,
    std::shared_ptr<graph::Graph> graph) {
  auto activator = platform::make_windows_asio_driver_activator();
  auto negotiator = platform::make_windows_asio_driver_negotiator();
  if (!activator || !negotiator) {
    return failure("physical_asio_runtime_resources_unavailable",
                   "Physical ASIO runtime dependencies could not be created.");
  }
  return open_windows_physical_asio_engine_runtime(
      configuration, std::move(graph), platform::probe_windows_asio_driver,
      *activator, *negotiator);
}

}  // namespace sar::service
