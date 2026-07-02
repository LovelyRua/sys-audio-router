#include "core/control/control_response.h"

#include <iostream>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  {
    const auto response = sar::control::command_accepted("cmd_1");
    if (const auto failure = expect(response.command_id == "cmd_1",
                                    "Expected accepted command ID")) {
      return failure;
    }
    if (const auto failure =
            expect(response.status == sar::control::ControlResponseStatus::Accepted,
                   "Expected accepted status")) {
      return failure;
    }
    if (const auto failure = expect(response.errors.empty(), "Expected no accepted errors")) {
      return failure;
    }
    if (const auto failure = expect(!response.has_diagnostics,
                                    "Expected no diagnostics on plain accepted response")) {
      return failure;
    }
  }

  {
    const auto response = sar::control::command_rejected(
        "cmd_2",
        {
            {"invalid_route_gain", "Route gain must be finite."},
        });
    if (const auto failure =
            expect(response.status == sar::control::ControlResponseStatus::Rejected,
                   "Expected rejected status")) {
      return failure;
    }
    if (const auto failure = expect(response.errors.size() == 1, "Expected one error")) {
      return failure;
    }
    if (const auto failure = expect(response.errors[0].code == "invalid_route_gain",
                                    "Expected rejected error code")) {
      return failure;
    }
  }

  {
    sar::diagnostics::EngineDiagnostics diagnostics;
    diagnostics.graph_version = 7;
    diagnostics.xrun_count = 3;
    const auto response = sar::control::diagnostics_response("cmd_3", diagnostics);
    if (const auto failure = expect(response.has_diagnostics,
                                    "Expected diagnostics payload")) {
      return failure;
    }
    if (const auto failure = expect(response.diagnostics.graph_version == 7,
                                    "Expected diagnostics graph version")) {
      return failure;
    }
    if (const auto failure = expect(response.diagnostics.xrun_count == 3,
                                    "Expected diagnostics xrun count")) {
      return failure;
    }
  }

  std::cout << "Control response smoke test passed\n";
  return 0;
}
