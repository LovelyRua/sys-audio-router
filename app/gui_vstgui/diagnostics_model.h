#pragma once

#include "app/gui_vstgui/engine_client.h"

#include <string>
#include <vector>

namespace sar::gui_vstgui {

enum class DiagnosticSeverity {
  Normal,
  Warning,
  Fault,
};

struct DiagnosticRow {
  std::string label;
  std::string value;
  DiagnosticSeverity severity = DiagnosticSeverity::Normal;
};

struct EndpointDiagnosticRow {
  std::string endpoint;
  std::string role;
  std::string health;
  std::string queue;
  std::string clock;
  std::string xruns;
  DiagnosticSeverity severity = DiagnosticSeverity::Normal;
};

struct DiagnosticsSnapshot {
  std::vector<DiagnosticRow> engine;
  std::vector<DiagnosticRow> recovery;
  std::vector<EndpointDiagnosticRow> endpoints;
};

[[nodiscard]] DiagnosticsSnapshot build_diagnostics_snapshot(
    const EngineState& state);

}  // namespace sar::gui_vstgui
