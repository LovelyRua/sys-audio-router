#pragma once

#include "core/control/control_command.h"
#include "core/service/engine_audio_runtime.h"
#include "core/platform/windows_asio_control_open.h"
#include "core/platform/windows_asio_driver_probe.h"

#include <functional>

namespace sar::service {

using WindowsPhysicalAsioProbe = std::function<
    platform::WindowsAsioDriverProbeResult(const std::string&)>;

// Physical ASIO currently has a deterministic direct-I/O graph boundary.
// Preset matrix routing is not applied until native ASIO ports participate in
// the unified matrix topology.
[[nodiscard]] std::shared_ptr<graph::Graph>
build_windows_physical_asio_direct_graph(
    const control::AudioRuntimeConfiguration& configuration,
    const platform::WindowsAsioDriverProbe& driver,
    std::uint64_t graph_version);

[[nodiscard]] EngineAudioRuntimeBuildResult
open_windows_physical_asio_engine_runtime(
    const control::AudioRuntimeConfiguration& configuration,
    std::shared_ptr<graph::Graph> graph,
    WindowsPhysicalAsioProbe probe,
    platform::WindowsAsioDriverActivator& activator,
    platform::WindowsAsioDriverNegotiator& negotiator);

[[nodiscard]] EngineAudioRuntimeBuildResult
open_windows_physical_asio_engine_runtime(
    const control::AudioRuntimeConfiguration& configuration,
    std::shared_ptr<graph::Graph> graph);

}  // namespace sar::service
