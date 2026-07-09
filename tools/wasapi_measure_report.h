#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include <iosfwd>

namespace sar::tools {

void print_wasapi_probe(std::ostream& out,
                        const char* label,
                        const platform::WasapiStreamProbe& probe);

void print_wasapi_runtime_summary(
    std::ostream& out,
    const platform::WasapiRuntimeSummary& summary);

void print_wasapi_stream_diagnostics(
    std::ostream& out,
    const char* label,
    const platform::WasapiStreamDiagnostics& diagnostics);

void print_wasapi_worker_stats(
    std::ostream& out,
    const platform::WasapiRealtimeWorkerStats& stats);

void print_wasapi_engine_diagnostics(
    std::ostream& out,
    const diagnostics::EngineDiagnostics& diagnostics);

}  // namespace sar::tools
