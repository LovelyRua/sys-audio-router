#include "app/gui_vstgui/diagnostics_model.h"

#include <cassert>
#include <optional>

int main() {
  using namespace sar;
  using namespace sar::gui_vstgui;

  EngineState healthy;
  healthy.transportOk = true;
  healthy.runtimeConfigured = true;
  healthy.runtimeRunning = true;
  healthy.diagnostics.graph_version = 17;
  healthy.diagnostics.processed_blocks = 4'800;
  healthy.diagnostics.xrun_count = 0;
  healthy.diagnostics.capture_fifo_fill_frames = 256;
  healthy.diagnostics.capture_fifo_overflow_frames = 0;
  healthy.diagnostics.render_fifo_fill_frames = 384;
  healthy.diagnostics.render_fifo_underflow_frames = 0;
  healthy.diagnostics.last_callback_seconds = 0.000012;
  healthy.diagnostics.peak_callback_seconds = 0.000034;
  healthy.diagnostics.virtual_asio_active_producers = 2;
  healthy.diagnostics.virtual_asio_clipped_samples = 0;
  healthy.hasWasapiRecovery = true;
  healthy.wasapiRecovery.state = control::WasapiRecoveryState::Running;
  healthy.wasapiRecovery.runtime_health = control::WasapiRuntimeHealth::Healthy;
  healthy.wasapiRecovery.recovery_episode_count = 1;
  healthy.wasapiRecovery.successful_recovery_count = 1;
  healthy.endpointDiagnostics.push_back({
      .endpoint_id = "main-render-endpoint",
      .role = control::AudioEndpointRuntimeRole::Master,
      .diagnostics = {},
      .queue_fill_frames = std::nullopt,
      .correction_ppm = std::nullopt,
  });
  healthy.endpointDiagnostics.push_back({
      .endpoint_id = "secondary-capture-endpoint-with-long-device-name",
      .role = control::AudioEndpointRuntimeRole::Follower,
      .diagnostics = {.xrun_count = 2},
      .queue_fill_frames = 512,
      .correction_ppm = -24.5,
  });

  const auto healthy_snapshot = build_diagnostics_snapshot(healthy);
  assert(healthy_snapshot.engine.size() == 9);
  assert(healthy_snapshot.recovery.size() == 9);
  assert(healthy_snapshot.engine[0].value == "Running");
  assert(healthy_snapshot.engine[0].severity == DiagnosticSeverity::Normal);
  assert(healthy_snapshot.engine[1].value == "v17  |  4800 blocks");
  assert(healthy_snapshot.engine[2].value == "0 / 0");
  assert(healthy_snapshot.engine[6].value == "12.0 / 34.0 us");
  assert(healthy_snapshot.recovery[0].value == "Healthy");
  assert(healthy_snapshot.recovery[4].value == "1 / 0");
  assert(healthy_snapshot.endpoints.size() == 2);
  assert(healthy_snapshot.endpoints[0].role == "MASTER");
  assert(healthy_snapshot.endpoints[0].queue == "-");
  assert(healthy_snapshot.endpoints[0].clock == "MASTER");
  assert(healthy_snapshot.endpoints[1].endpoint.size() == 28);
  assert(healthy_snapshot.endpoints[1].endpoint.ends_with("..."));
  assert(healthy_snapshot.endpoints[1].queue == "512 f");
  assert(healthy_snapshot.endpoints[1].clock == "-24.5 ppm");
  assert(healthy_snapshot.endpoints[1].xruns == "2");
  assert(healthy_snapshot.endpoints[1].severity == DiagnosticSeverity::Fault);

  EngineState degraded = healthy;
  degraded.diagnostics.xrun_count = 1;
  degraded.diagnostics.virtual_asio_producer_underflows = 3;
  degraded.diagnostics.virtual_asio_producer_overflows = 1;
  degraded.diagnostics.capture_fifo_overflow_frames = 1;
  degraded.diagnostics.render_fifo_underflow_frames = 1;
  degraded.wasapiRecovery.runtime_health = control::WasapiRuntimeHealth::Degraded;
  degraded.wasapiRecovery.runtime_reason_code = "capture_wait_timeout";
  degraded.wasapiRecovery.wait_timeout_cycles = 4;
  degraded.wasapiRecovery.failed_recovery_count = 1;
  const auto degraded_snapshot = build_diagnostics_snapshot(degraded);
  assert(degraded_snapshot.engine[2].severity == DiagnosticSeverity::Fault);
  assert(degraded_snapshot.engine[3].severity == DiagnosticSeverity::Fault);
  assert(degraded_snapshot.engine[4].severity == DiagnosticSeverity::Warning);
  assert(degraded_snapshot.engine[5].severity == DiagnosticSeverity::Warning);
  assert(degraded_snapshot.recovery[0].severity == DiagnosticSeverity::Warning);
  assert(degraded_snapshot.recovery[2].value == "capture_wait_timeout");
  assert(degraded_snapshot.recovery[3].value == "4 / 0");
  assert(degraded_snapshot.recovery[4].severity == DiagnosticSeverity::Fault);

  EngineState offline;
  const auto offline_snapshot = build_diagnostics_snapshot(offline);
  assert(offline_snapshot.engine[0].value == "Offline");
  assert(offline_snapshot.engine[0].severity == DiagnosticSeverity::Fault);
  assert(offline_snapshot.recovery[0].value == "No WASAPI recovery data");
  assert(offline_snapshot.endpoints.empty());
  return 0;
}
