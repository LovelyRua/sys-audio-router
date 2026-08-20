#pragma once

#include "core/control/control_command.h"
#include "core/service/engine_audio_runtime.h"
#include "core/platform/windows_asio_control_open.h"
#include "core/platform/windows_asio_driver_probe.h"

#include <functional>

namespace sar::service {

using WindowsPhysicalAsioProbe = std::function<
    platform::WindowsAsioDriverProbeResult(const std::string&)>;

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
