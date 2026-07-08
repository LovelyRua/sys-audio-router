#pragma once

#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include <vector>

namespace sar::platform {

[[nodiscard]] bool compatible_wasapi_duplex_sample_rates(
    const WasapiStreamProbe& capture_probe,
    const WasapiStreamProbe& render_probe) noexcept;

[[nodiscard]] std::vector<WasapiRealtimeWorkerError>
validate_wasapi_render_graph_preflight(const graph::Graph& graph,
                                       const WasapiStreamProbe& render_probe);

[[nodiscard]] std::vector<WasapiRealtimeWorkerError>
validate_wasapi_duplex_graph_preflight(const graph::Graph& graph,
                                       const WasapiStreamProbe& capture_probe,
                                       const WasapiStreamProbe& render_probe);

}  // namespace sar::platform
