#include "app/gui_vstgui/diagnostics_model.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace sar::gui_vstgui {
namespace {

using control::AudioEndpointRuntimeRole;
using control::WasapiRecoveryState;
using control::WasapiRuntimeHealth;

std::string number(const char* format, std::uint64_t value) {
  char buffer[64]{};
  std::snprintf(buffer, sizeof(buffer), format,
                static_cast<unsigned long long>(value));
  return buffer;
}

std::string decimal(const char* format, double value) {
  char buffer[64]{};
  std::snprintf(buffer, sizeof(buffer), format, value);
  return buffer;
}

DiagnosticSeverity severity_for_count(std::uint64_t value) {
  return value == 0 ? DiagnosticSeverity::Normal : DiagnosticSeverity::Warning;
}

const char* runtime_health_name(WasapiRuntimeHealth health) {
  switch (health) {
    case WasapiRuntimeHealth::Stopped: return "Stopped";
    case WasapiRuntimeHealth::Healthy: return "Healthy";
    case WasapiRuntimeHealth::Degraded: return "Degraded";
    case WasapiRuntimeHealth::Faulted: return "Faulted";
  }
  return "Unknown";
}

const char* recovery_state_name(WasapiRecoveryState state) {
  switch (state) {
    case WasapiRecoveryState::Stopped: return "Stopped";
    case WasapiRecoveryState::Opening: return "Opening";
    case WasapiRecoveryState::Running: return "Running";
    case WasapiRecoveryState::Quiescing: return "Quiescing";
    case WasapiRecoveryState::Backoff: return "Backoff";
    case WasapiRecoveryState::Faulted: return "Faulted";
  }
  return "Unknown";
}

std::string compact_endpoint(std::string value) {
  constexpr std::size_t kMaximumEndpointLabel = 28;
  if (value.size() > kMaximumEndpointLabel) {
    value.resize(kMaximumEndpointLabel - 3);
    value += "...";
  }
  return value;
}

}  // namespace

DiagnosticsSnapshot build_diagnostics_snapshot(const EngineState& state) {
  DiagnosticsSnapshot snapshot;
  const auto& d = state.diagnostics;
  const auto engine_severity = state.transportOk ? DiagnosticSeverity::Normal
                                                 : DiagnosticSeverity::Fault;
  snapshot.engine = {
      {"ENGINE", !state.transportOk ? "Offline"
                 : !state.runtimeConfigured ? "Audio runtime not configured"
                 : state.runtimeRunning ? "Running"
                                        : "Configured, stopped",
       engine_severity},
      {"GRAPH", "v" + std::to_string(d.graph_version) + "  |  " +
                    number("%llu blocks", d.processed_blocks),
       DiagnosticSeverity::Normal},
      {"XRUN / DROPPED", number("%llu / ", d.xrun_count) +
                             number("%llu", d.virtual_asio_dropped_blocks),
       d.xrun_count == 0 && d.virtual_asio_dropped_blocks == 0
           ? DiagnosticSeverity::Normal : DiagnosticSeverity::Fault},
      {"PRODUCER UNDER / OVER", number("%llu / ", d.virtual_asio_producer_underflows) +
                                    number("%llu", d.virtual_asio_producer_overflows),
       d.virtual_asio_producer_overflows > 0 ? DiagnosticSeverity::Fault
       : severity_for_count(d.virtual_asio_producer_underflows)},
      {"CAPTURE FIFO", number("%llu f  |  ", d.capture_fifo_fill_frames) +
                            number("%llu dropped f",
                                   d.capture_fifo_overflow_frames) + "  |  " +
                            number("%llu cycles", d.capture_fifo_overflow_cycles),
       d.capture_fifo_overflow_cycles == 0 &&
               d.capture_fifo_overflow_frames == 0
           ? DiagnosticSeverity::Normal : DiagnosticSeverity::Warning},
      {"RENDER FIFO", number("%llu f  |  ", d.render_fifo_fill_frames) +
                           number("%llu underflow f  |  ",
                                  d.render_fifo_underflow_frames) +
                           number("%llu cycles", d.render_fifo_underflow_cycles),
       d.render_fifo_underflow_cycles == 0 &&
               d.render_fifo_underflow_frames == 0
           ? DiagnosticSeverity::Normal : DiagnosticSeverity::Warning},
      {"CALLBACK LAST / PEAK", decimal("%.1f / ", d.last_callback_seconds * 1.0e6) +
                                   decimal("%.1f us", d.peak_callback_seconds * 1.0e6),
       DiagnosticSeverity::Normal},
      {"SAMPLE CONVERSION ERRORS", number("%llu import / ",
                                           d.sample_conversion_import_failures) +
                                       number("%llu export",
                                           d.sample_conversion_export_failures),
       d.sample_conversion_import_failures == 0 &&
               d.sample_conversion_export_failures == 0
           ? DiagnosticSeverity::Normal : DiagnosticSeverity::Fault},
      {"ASIO CLIENTS / CLIPPED", number("%llu active  |  ",
                                         d.virtual_asio_active_producers) +
                                      number("%llu clipped",
                                             d.virtual_asio_clipped_samples),
       d.virtual_asio_clipped_samples > 0 ? DiagnosticSeverity::Warning
                                          : DiagnosticSeverity::Normal},
  };

  const auto recovery_severity = [&] {
    if (!state.hasWasapiRecovery) return DiagnosticSeverity::Normal;
    if (state.wasapiRecovery.runtime_health == WasapiRuntimeHealth::Faulted)
      return DiagnosticSeverity::Fault;
    if (state.wasapiRecovery.runtime_health == WasapiRuntimeHealth::Degraded)
      return DiagnosticSeverity::Warning;
    return DiagnosticSeverity::Normal;
  }();
  const auto& r = state.wasapiRecovery;
  snapshot.recovery = {
      {"WASAPI HEALTH", state.hasWasapiRecovery
                            ? runtime_health_name(r.runtime_health)
                            : "No WASAPI recovery data",
       recovery_severity},
      {"RECOVERY STATE", state.hasWasapiRecovery
                             ? recovery_state_name(r.state) : "Unavailable",
       recovery_severity},
      {"HEALTH REASON", r.runtime_reason_code.empty()
                            ? "-" : r.runtime_reason_code,
       recovery_severity},
      {"WAIT / DISCONTINUITY", number("%llu / ", r.wait_timeout_cycles) +
                                    number("%llu", r.capture_discontinuity_cycles),
       r.wait_timeout_cycles > 0 ? DiagnosticSeverity::Warning
       : severity_for_count(r.capture_discontinuity_cycles)},
      {"RECOVERY OK / FAILED", number("%llu / ", r.successful_recovery_count) +
                                   number("%llu", r.failed_recovery_count),
       r.failed_recovery_count > 0 ? DiagnosticSeverity::Fault
                                   : DiagnosticSeverity::Normal},
      {"RECOVERY LAST / MAX", number("%llu / ", r.last_recovery_duration_ms) +
                                   number("%llu ms", r.maximum_recovery_duration_ms),
       r.failed_recovery_count > 0 ? DiagnosticSeverity::Fault
                                   : DiagnosticSeverity::Normal},
      {"ENDPOINT REOPEN / RESET", number("%llu / ",
                                          r.endpoint_notification_reopen_count) +
                                       number("%llu", r.endpoint_notification_reset_failure_count),
       r.endpoint_notification_reset_failure_count > 0
           ? DiagnosticSeverity::Fault : DiagnosticSeverity::Normal},
      {"MAX RECOVERY SILENCE", number("%llu frames",
                                       r.maximum_render_recovery_silence_frames),
       severity_for_count(r.maximum_render_recovery_silence_frames)},
      {"MAX CAPTURE RATE CLAMP", number("%llu frames",
          r.maximum_consecutive_capture_rate_clamped_frames),
       severity_for_count(r.maximum_consecutive_capture_rate_clamped_frames)},
  };

  snapshot.endpoints.reserve(state.endpointDiagnostics.size());
  for (const auto& endpoint : state.endpointDiagnostics) {
    EndpointDiagnosticRow row;
    row.endpoint = compact_endpoint(endpoint.endpoint_id);
    row.role = endpoint.role == AudioEndpointRuntimeRole::Master ? "MASTER" : "FOLLOWER";
    row.health = "Healthy";
    row.severity = endpoint.diagnostics.xrun_count == 0
                       ? DiagnosticSeverity::Normal : DiagnosticSeverity::Fault;
    if (endpoint.recovery) {
      const auto& endpoint_recovery = *endpoint.recovery;
      row.health = runtime_health_name(endpoint_recovery.runtime_health);
      if (!endpoint_recovery.runtime_reason_code.empty()) {
        row.health += ":" + endpoint_recovery.runtime_reason_code;
        row.health = compact_endpoint(std::move(row.health));
      }
      if (endpoint_recovery.runtime_health == WasapiRuntimeHealth::Faulted) {
        row.severity = DiagnosticSeverity::Fault;
      } else if (endpoint_recovery.runtime_health == WasapiRuntimeHealth::Degraded &&
                 row.severity != DiagnosticSeverity::Fault) {
        row.severity = DiagnosticSeverity::Warning;
      }
    }
    row.queue = endpoint.queue_fill_frames
                    ? number("%llu f", *endpoint.queue_fill_frames) : "-";
    row.clock = endpoint.correction_ppm
                    ? decimal("%+.1f ppm", *endpoint.correction_ppm) : "MASTER";
    row.xruns = number("%llu", endpoint.diagnostics.xrun_count);
    snapshot.endpoints.push_back(std::move(row));
  }
  return snapshot;
}

}  // namespace sar::gui_vstgui
