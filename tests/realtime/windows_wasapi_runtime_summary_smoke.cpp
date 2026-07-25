#include "core/platform/windows_wasapi_runtime_summary.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::platform::WasapiRealtimeWorkerStats make_active_stats() {
  sar::platform::WasapiRealtimeWorkerStats stats;
  stats.loop_cycles = 8;
  stats.graph_processed_cycles = 8;
  stats.captured_frames = 256;
  stats.rendered_frames = 256;
  stats.last_captured_frames = 64;
  stats.last_rendered_frames = 64;
  stats.last_graph_processed = true;
  return stats;
}

int expect_summary(const sar::platform::WasapiRuntimeSummary& summary,
                   sar::platform::WasapiRuntimeHealth health,
                   const std::string& reason_code,
                   const char* label) {
  if (const auto f = expect(summary.health == health, label)) return f;
  if (const auto f = expect(summary.reason_code == reason_code, label)) return f;
  if (const auto f = expect(!summary.reason.empty(), label)) return f;
  return 0;
}

}  // namespace

int main() {
  // Health name checks.
  if (const auto f = expect(
          std::string(sar::platform::wasapi_runtime_health_name(
              sar::platform::WasapiRuntimeHealth::Stopped)) == "stopped",
          "Expected stopped health name")) {
    return f;
  }
  if (const auto f = expect(
          std::string(sar::platform::wasapi_runtime_health_name(
              sar::platform::WasapiRuntimeHealth::Healthy)) == "healthy",
          "Expected healthy health name")) {
    return f;
  }
  if (const auto f = expect(
          std::string(sar::platform::wasapi_runtime_health_name(
              sar::platform::WasapiRuntimeHealth::Degraded)) == "degraded",
          "Expected degraded health name")) {
    return f;
  }
  if (const auto f = expect(
          std::string(sar::platform::wasapi_runtime_health_name(
              sar::platform::WasapiRuntimeHealth::Faulted)) == "faulted",
          "Expected faulted health name")) {
    return f;
  }

  // Empty stats -> stopped summary.
  {
    const sar::platform::WasapiRealtimeWorkerStats stats;
    const auto summary =
        sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto f =
            expect_summary(summary, sar::platform::WasapiRuntimeHealth::Stopped,
                           "no_cycles", "Expected no-cycle summary")) {
      return f;
    }
    if (const auto f = expect(!summary.has_capture_stream,
                              "Expected no capture stream")) {
      return f;
    }
    if (const auto f = expect(!summary.has_render_stream,
                              "Expected no render stream")) {
      return f;
    }
    if (const auto f = expect(summary.error_count == 0, "Expected no errors")) {
      return f;
    }
    // Verify the new fields appear in the machine-readable line.
    const auto line =
        sar::platform::format_wasapi_runtime_summary_line(summary);
    if (const auto f =
            expect(line.find("sample_conversion_import_failures=0") !=
                       std::string::npos,
                   "Expected sample_conversion_import_failures in summary line")) {
      return f;
    }
    if (const auto f =
            expect(line.find("sample_conversion_export_failures=0") !=
                       std::string::npos,
                   "Expected sample_conversion_export_failures in summary line")) {
      return f;
    }
  }

  // Active stats with sample conversion import failures -> degraded.
  {
    auto stats = make_active_stats();
    sar::diagnostics::EngineDiagnostics diagnostics;
    diagnostics.sample_conversion_import_failures = 2;
    const auto summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, nullptr, nullptr, &diagnostics);
    if (const auto f =
            expect(summary.sample_conversion_import_failures == 2,
                   "Expected copied import failures")) {
      return f;
    }
    if (const auto f =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "sample_conversion_import_failure",
                           "Expected import-only failure health")) {
      return f;
    }
  }

  // Active stats with sample conversion export failures -> degraded.
  {
    auto stats = make_active_stats();
    sar::diagnostics::EngineDiagnostics diagnostics;
    diagnostics.sample_conversion_export_failures = 3;
    const auto summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, nullptr, nullptr, &diagnostics);
    if (const auto f =
            expect(summary.sample_conversion_export_failures == 3,
                   "Expected copied export failures")) {
      return f;
    }
    if (const auto f =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "sample_conversion_export_failure",
                           "Expected export-only failure health")) {
      return f;
    }
  }

  // Active stats with both failures -> degraded, combined reason.
  {
    auto stats = make_active_stats();
    sar::diagnostics::EngineDiagnostics diagnostics;
    diagnostics.sample_conversion_import_failures = 1;
    diagnostics.sample_conversion_export_failures = 1;
    const auto summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, nullptr, nullptr, &diagnostics);
    if (const auto f =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "sample_conversion_failure",
                           "Expected combined failure health")) {
      return f;
    }
    const auto line =
        sar::platform::format_wasapi_runtime_summary_line(summary);
    if (const auto f =
            expect(line.find("sample_conversion_import_failures=1") !=
                       std::string::npos,
                   "Expected import=1 in line")) {
      return f;
    }
    if (const auto f =
            expect(line.find("sample_conversion_export_failures=1") !=
                       std::string::npos,
                   "Expected export=1 in line")) {
      return f;
    }
  }

  // Active stats with no failures -> healthy.
  {
    auto stats = make_active_stats();
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, nullptr, nullptr, &diagnostics);
    if (const auto f =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Healthy,
                           "running",
                           "Expected healthy summary with no failures")) {
      return f;
    }
  }

  // should_fail logic.
  {
    sar::platform::WasapiRuntimeSummary faulted;
    faulted.health = sar::platform::WasapiRuntimeHealth::Faulted;
    if (const auto f =
            expect(sar::platform::wasapi_runtime_summary_should_fail(faulted, false),
                   "Expected should_fail for faulted")) {
      return f;
    }
    sar::platform::WasapiRuntimeSummary healthy;
    healthy.health = sar::platform::WasapiRuntimeHealth::Healthy;
    if (const auto f =
            expect(!sar::platform::wasapi_runtime_summary_should_fail(healthy, false),
                   "Expected should_pass for healthy")) {
      return f;
    }
    if (const auto f =
            expect(!sar::platform::wasapi_runtime_summary_should_fail(healthy, true),
                   "Expected should_pass for healthy with require_healthy")) {
      return f;
    }
    sar::platform::WasapiRuntimeSummary degraded;
    degraded.health = sar::platform::WasapiRuntimeHealth::Degraded;
    if (const auto f =
            expect(!sar::platform::wasapi_runtime_summary_should_fail(degraded, false),
                   "Expected should_pass for degraded without require_healthy")) {
      return f;
    }
    if (const auto f =
            expect(sar::platform::wasapi_runtime_summary_should_fail(degraded, true),
                   "Expected should_fail for degraded with require_healthy")) {
      return f;
    }
  }

  std::cout << "WASAPI runtime summary smoke test passed\n";
  return 0;
}