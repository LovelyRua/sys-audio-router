#pragma once

#include "core/control/control_command.h"
#include "core/control/preset_document.h"
#include "core/platform/realtime_audio_source.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "core/service/engine_audio_runtime.h"

#include <memory>
#include <string>
#include <vector>

namespace sar::service {

struct WindowsWasapiMatrixEndpointResourceDiagnostics {
  std::string endpoint_id;
  diagnostics::EngineDiagnostics diagnostics;
  std::uint64_t queue_fill_frames = 0;
  double correction_ppm = 0.0;
};

struct WindowsVirtualWasapiMatrixInput {
  std::wstring mapping_name;
  std::size_t graph_first_channel = 0;
};

void merge_windows_wasapi_matrix_endpoint_diagnostics(
    std::vector<EngineAudioEndpointDiagnostics>& endpoints,
    const std::vector<WindowsWasapiMatrixEndpointResourceDiagnostics>&
        resources) noexcept;

// Builds one render-clock master plus independently rate-matched inputs and
// followers. Virtual inputs are explicit pre-existing graph channel ranges;
// the control configuration does not expose them yet.
[[nodiscard]] EngineAudioRuntimeBuildResult open_windows_wasapi_matrix_runtime(
    const control::AudioRuntimeConfiguration& configuration,
    const control::PresetRouteMatrix& matrix,
    std::shared_ptr<graph::Graph> graph,
    platform::RealtimeAudioSource* external_input = nullptr,
    platform::RealtimeAudioSink* external_output = nullptr,
    platform::WasapiGraphChannelLayout base_layout = {},
    const std::vector<WindowsVirtualWasapiMatrixInput>& virtual_inputs = {});

}  // namespace sar::service
