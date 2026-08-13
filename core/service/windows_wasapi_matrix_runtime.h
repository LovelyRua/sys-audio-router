#pragma once

#include "core/control/control_command.h"
#include "core/control/preset_document.h"
#include "core/platform/realtime_audio_source.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "core/service/engine_audio_runtime.h"

#include <memory>

namespace sar::service {

// Builds one render-clock master plus independently rate-matched render and
// capture followers. Every physical endpoint owns its device clock; only the
// render master advances the product graph.
[[nodiscard]] EngineAudioRuntimeBuildResult open_windows_wasapi_matrix_runtime(
    const control::AudioRuntimeConfiguration& configuration,
    const control::PresetRouteMatrix& matrix,
    std::shared_ptr<graph::Graph> graph,
    platform::RealtimeAudioSource* external_input = nullptr,
    platform::RealtimeAudioSink* external_output = nullptr,
    platform::WasapiGraphChannelLayout base_layout = {});

}  // namespace sar::service
