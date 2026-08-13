#include "core/service/windows_wasapi_matrix_runtime.h"

#include "core/platform/realtime_audio_channel_slice_sink.h"
#include "core/platform/realtime_audio_endpoint_queue.h"
#include "core/platform/realtime_audio_fanout_sink.h"
#include "core/platform/realtime_audio_rate_matching_source.h"
#include "core/platform/windows_wasapi_stream.h"
#include "core/service/audio_runtime_matrix_binding.h"
#include "core/service/audio_runtime_topology.h"
#include "core/service/multi_endpoint_audio_runtime.h"
#include "core/service/windows_wasapi_engine_runtime.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace sar::service {

namespace {

constexpr std::size_t kEndpointQueueCapacityBlocks = 32;

struct RenderFollowerResources {
  std::unique_ptr<platform::RealtimeAudioEndpointQueue> queue;
  std::unique_ptr<platform::RealtimeAudioRateMatchingSource> rate_matcher;
  std::shared_ptr<graph::Graph> graph;
};

class WindowsWasapiMatrixRuntime final : public EngineAudioRuntime {
 public:
  WindowsWasapiMatrixRuntime(
      std::vector<std::unique_ptr<RenderFollowerResources>> resources,
      std::unique_ptr<platform::RealtimeAudioChannelSliceSink>
          external_output_slice,
      std::unique_ptr<platform::RealtimeAudioFanoutSink> fanout,
      std::unique_ptr<MultiEndpointAudioRuntime> runtime)
      : resources_(std::move(resources)),
        external_output_slice_(std::move(external_output_slice)),
        fanout_(std::move(fanout)),
        runtime_(std::move(runtime)) {}

  ~WindowsWasapiMatrixRuntime() override { stop(); }

  EngineAudioRuntimeResult start(std::uint32_t timeout_ms) override {
    return runtime_->start(timeout_ms);
  }
  void stop() noexcept override { runtime_->stop(); }
  bool running() const noexcept override { return runtime_->running(); }
  std::uint64_t graph_version() const noexcept override {
    return runtime_->graph_version();
  }
  bool apply_realtime_graph_parameters(
      const graph::Graph& graph) noexcept override {
    return runtime_->apply_realtime_graph_parameters(graph);
  }
  diagnostics::EngineDiagnostics diagnostics() const override {
    return runtime_->diagnostics();
  }
  std::optional<EngineAudioRecoveryDiagnostics> recovery_diagnostics()
      const override {
    return runtime_->recovery_diagnostics();
  }

 private:
  std::vector<std::unique_ptr<RenderFollowerResources>> resources_;
  std::unique_ptr<platform::RealtimeAudioChannelSliceSink>
      external_output_slice_;
  std::unique_ptr<platform::RealtimeAudioFanoutSink> fanout_;
  std::unique_ptr<MultiEndpointAudioRuntime> runtime_;
};

EngineAudioRuntimeBuildResult failure(std::string code, std::string message) {
  return EngineAudioRuntimeBuildResult::failure(
      {{std::move(code), std::move(message)}});
}

EngineAudioRuntimeBuildResult failure(
    const std::vector<control::PresetError>& errors) {
  std::vector<EngineAudioRuntimeError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return EngineAudioRuntimeBuildResult::failure(std::move(converted));
}

EngineAudioRuntimeBuildResult failure(
    const std::vector<platform::WasapiStreamProbeError>& errors) {
  std::vector<EngineAudioRuntimeError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return EngineAudioRuntimeBuildResult::failure(std::move(converted));
}

platform::WasapiStreamProbeResult probe_render(const std::string& device_id) {
  return device_id.empty()
             ? platform::probe_default_wasapi_stream(
                   platform::WasapiStreamDirection::Render)
             : platform::probe_wasapi_stream(
                   device_id, platform::WasapiStreamDirection::Render);
}

platform::WasapiStreamProbeResult probe_capture(const std::string& device_id) {
  return device_id.empty()
             ? platform::probe_default_wasapi_stream(
                   platform::WasapiStreamDirection::Capture)
             : platform::probe_wasapi_stream(
                   device_id, platform::WasapiStreamDirection::Capture);
}

const AudioRuntimeEndpointGraphBinding* find_binding(
    const std::vector<AudioRuntimeEndpointGraphBinding>& bindings,
    std::string_view endpoint_id) {
  const auto found = std::ranges::find_if(
      bindings, [endpoint_id](const auto& binding) {
        return binding.endpoint_id == endpoint_id;
      });
  return found == bindings.end() ? nullptr : &*found;
}

bool endpoint_uses_entire_device(
    const control::AudioRuntimeEndpointConfiguration& endpoint,
    const platform::WasapiStreamProbe& probe) noexcept {
  return endpoint.first_channel == 0 &&
         endpoint.channel_count == probe.mix_format.channels;
}

}  // namespace

EngineAudioRuntimeBuildResult open_windows_wasapi_matrix_runtime(
    const control::AudioRuntimeConfiguration& configuration,
    const control::PresetRouteMatrix& matrix,
    std::shared_ptr<graph::Graph> graph,
    platform::RealtimeAudioSource* external_input,
    platform::RealtimeAudioSink* external_output,
    platform::WasapiGraphChannelLayout base_layout) {
  if (!graph) {
    return failure("null_runtime_graph",
                   "WASAPI matrix runtime requires a graph.");
  }
  const auto topology_result = build_audio_runtime_topology(configuration);
  if (!topology_result.ok()) {
    return failure(topology_result.errors());
  }
  const auto binding_result =
      bind_audio_runtime_to_matrix(topology_result.topology(), matrix);
  if (!binding_result.ok()) {
    return failure(binding_result.errors());
  }
  const auto& topology = topology_result.topology();
  const auto& bindings = binding_result.bindings();
  const auto& master_endpoint =
      topology.endpoints[topology.clock_master_index];
  const auto* master_binding =
      find_binding(bindings, master_endpoint.endpoint_id);
  if (master_binding == nullptr) {
    return failure("audio_runtime_master_binding_missing",
                   "Matrix clock master has no graph channel binding.");
  }

  const auto master_probe = probe_render(master_endpoint.device_id);
  if (!master_probe.ok()) {
    return failure(master_probe.errors());
  }
  if (!endpoint_uses_entire_device(master_endpoint, master_probe.probe())) {
    return failure(
        "wasapi_matrix_partial_native_channel_range_not_supported",
        "Alpha matrix runtime currently requires each endpoint channel range "
        "to cover the complete native device.");
  }

  std::vector<const control::AudioRuntimeEndpointConfiguration*>
      capture_endpoints;
  std::vector<const control::AudioRuntimeEndpointConfiguration*>
      follower_endpoints;
  for (const auto& endpoint : topology.endpoints) {
    if (endpoint.direction ==
        control::AudioRuntimeEndpointDirection::Capture) {
      capture_endpoints.push_back(&endpoint);
    } else if (!endpoint.clock_master) {
      follower_endpoints.push_back(&endpoint);
    }
  }
  if (capture_endpoints.size() > 1) {
    return failure("multiple_wasapi_capture_followers_not_supported",
                   "Alpha matrix runtime supports at most one physical "
                   "capture endpoint.");
  }

  std::vector<std::unique_ptr<RenderFollowerResources>> resources;
  std::vector<AudioRuntimeMember> follower_runtimes;
  std::vector<platform::RealtimeAudioSink*> fanout_sinks;
  resources.reserve(follower_endpoints.size());
  follower_runtimes.reserve(follower_endpoints.size());
  fanout_sinks.reserve(follower_endpoints.size() +
                       static_cast<std::size_t>(external_output != nullptr));

  for (const auto* endpoint : follower_endpoints) {
    const auto* binding = find_binding(bindings, endpoint->endpoint_id);
    if (binding == nullptr) {
      return failure("audio_runtime_follower_binding_missing",
                     "Render follower has no graph channel binding.");
    }
    const auto probe = probe_render(endpoint->device_id);
    if (!probe.ok()) {
      return failure(probe.errors());
    }
    if (!endpoint_uses_entire_device(*endpoint, probe.probe())) {
      return failure(
          "wasapi_matrix_partial_native_channel_range_not_supported",
          "Alpha matrix runtime currently requires each endpoint channel "
          "range to cover the complete native device.");
    }

    auto owned = std::make_unique<RenderFollowerResources>();
    owned->queue = std::make_unique<platform::RealtimeAudioEndpointQueue>(
        binding->graph_first_channel, binding->channel_count, graph->frames(),
        kEndpointQueueCapacityBlocks);
    owned->rate_matcher =
        std::make_unique<platform::RealtimeAudioRateMatchingSource>(
            *owned->queue, graph->sample_rate(),
            probe.probe().mix_format.sample_rate);
    owned->graph = std::make_shared<graph::Graph>(
        graph->version(), probe.probe().mix_format.channels, graph->frames(),
        probe.probe().mix_format.sample_rate);
    platform::WasapiGraphChannelLayout follower_layout{
        .graph_input_channels = probe.probe().mix_format.channels,
        .graph_output_channels = probe.probe().mix_format.channels,
        .external_input_offset = 0,
        .external_input_channels = probe.probe().mix_format.channels,
        .render_output_offset = 0,
    };
    auto opened = endpoint->device_id.empty()
                      ? WindowsWasapiEngineRuntime::open_default_render(
                            owned->graph, owned->rate_matcher.get(),
                            follower_layout)
                      : WindowsWasapiEngineRuntime::open_render(
                            endpoint->device_id, owned->graph,
                            owned->rate_matcher.get(), follower_layout);
    if (!opened.ok()) {
      return EngineAudioRuntimeBuildResult::failure(opened.errors());
    }
    fanout_sinks.push_back(&owned->queue->publisher());
    follower_runtimes.push_back(
        {endpoint->endpoint_id, opened.take_runtime()});
    resources.push_back(std::move(owned));
  }

  std::unique_ptr<platform::RealtimeAudioChannelSliceSink>
      external_output_slice;
  if (external_output != nullptr) {
    if (base_layout.external_output_channels == 0) {
      return failure("empty_external_output_channel_range",
                     "Matrix external output requires a channel range.");
    }
    external_output_slice =
        std::make_unique<platform::RealtimeAudioChannelSliceSink>(
            base_layout.external_output_offset,
            base_layout.external_output_channels, graph->frames(),
            *external_output);
    fanout_sinks.push_back(external_output_slice.get());
  }
  auto fanout = fanout_sinks.empty()
                    ? std::unique_ptr<platform::RealtimeAudioFanoutSink>{}
                    : std::make_unique<platform::RealtimeAudioFanoutSink>(
                          std::move(fanout_sinks));

  auto master_layout = base_layout;
  master_layout.graph_input_channels = graph->channels();
  master_layout.graph_output_channels = graph->channels();
  master_layout.render_output_offset = master_binding->graph_first_channel;
  if (fanout) {
    master_layout.external_output_offset = 0;
    master_layout.external_output_channels = graph->channels();
  }

  std::optional<WindowsWasapiEngineRuntimeOpenResult> master_opened;
  if (capture_endpoints.empty()) {
    master_opened.emplace(
        master_endpoint.device_id.empty()
            ? WindowsWasapiEngineRuntime::open_default_render(
                  graph, external_input, master_layout, fanout.get())
            : WindowsWasapiEngineRuntime::open_render(
                  master_endpoint.device_id, graph, external_input,
                  master_layout, fanout.get()));
  } else {
    const auto& capture = *capture_endpoints.front();
    const auto* capture_binding =
        find_binding(bindings, capture.endpoint_id);
    if (capture_binding == nullptr) {
      return failure("audio_runtime_capture_binding_missing",
                     "Capture endpoint has no graph channel binding.");
    }
    const auto capture_probe = probe_capture(capture.device_id);
    if (!capture_probe.ok()) {
      return failure(capture_probe.errors());
    }
    if (!endpoint_uses_entire_device(capture, capture_probe.probe())) {
      return failure(
          "wasapi_matrix_partial_native_channel_range_not_supported",
          "Alpha matrix runtime currently requires each endpoint channel "
          "range to cover the complete native device.");
    }
    master_layout.capture_input_offset =
        capture_binding->graph_first_channel;
    if (capture.device_id.empty() != master_endpoint.device_id.empty()) {
      return failure(
          "mixed_default_and_pinned_duplex_not_supported",
          "Matrix master duplex must use either two default endpoints or two "
          "explicit endpoint IDs.");
    }
    master_opened.emplace(
        capture.device_id.empty()
            ? WindowsWasapiEngineRuntime::open_default_duplex(
                  graph, external_input, fanout.get(), master_layout)
            : WindowsWasapiEngineRuntime::open_duplex(
                  capture.device_id, master_endpoint.device_id, graph,
                  external_input, fanout.get(), master_layout));
  }
  if (!master_opened->ok()) {
    return EngineAudioRuntimeBuildResult::failure(master_opened->errors());
  }

  auto coordinator = std::make_unique<MultiEndpointAudioRuntime>(
      AudioRuntimeMember{master_endpoint.endpoint_id,
                         master_opened->take_runtime()},
      std::move(follower_runtimes));
  auto runtime = std::make_unique<WindowsWasapiMatrixRuntime>(
      std::move(resources), std::move(external_output_slice),
      std::move(fanout), std::move(coordinator));
  return EngineAudioRuntimeBuildResult::success(std::move(runtime));
}

}  // namespace sar::service
