#include "core/platform/windows_wasapi_loop_preflight.h"

#include <algorithm>

namespace sar::platform {

bool compatible_wasapi_duplex_sample_rates(
    const WasapiStreamProbe& capture_probe,
    const WasapiStreamProbe& render_probe) noexcept {
  return capture_probe.mix_format.sample_rate == render_probe.mix_format.sample_rate;
}

std::vector<WasapiRealtimeWorkerError> validate_wasapi_render_graph_preflight(
    const graph::Graph& graph,
    const WasapiStreamProbe& render_probe) {
  if (graph.sample_rate() != render_probe.mix_format.sample_rate) {
    return {
        {
            "graph_sample_rate_mismatch",
            "Graph sample rate must match the WASAPI render stream sample rate.",
        },
    };
  }

  if (graph.node_count() <= 1) {
    return {};
  }

  if (graph.channels() >= render_probe.mix_format.channels &&
      graph.frames() >= render_probe.buffer_frames) {
    return {};
  }

  return {
      {
          "graph_buffer_too_small",
          "Graph scratch buffers must cover the WASAPI render stream shape.",
      },
  };
}

std::vector<WasapiRealtimeWorkerError> validate_wasapi_duplex_graph_preflight(
    const graph::Graph& graph,
    const WasapiStreamProbe& capture_probe,
    const WasapiStreamProbe& render_probe) {
  if (graph.sample_rate() != render_probe.mix_format.sample_rate) {
    return {
        {
            "graph_sample_rate_mismatch",
            "Graph sample rate must match the WASAPI render stream sample rate.",
        },
    };
  }

  if (graph.node_count() <= 1) {
    return {};
  }

  const auto required_channels =
      std::max(capture_probe.mix_format.channels, render_probe.mix_format.channels);
  const auto required_frames =
      std::max(capture_probe.buffer_frames, render_probe.buffer_frames);
  if (graph.channels() >= required_channels && graph.frames() >= required_frames) {
    return {};
  }

  return {
      {
          "graph_buffer_too_small",
          "Graph scratch buffers must cover the WASAPI duplex stream shapes.",
      },
  };
}

}  // namespace sar::platform
