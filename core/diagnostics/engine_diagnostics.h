#pragma once

#include <cstdint>

namespace sar::diagnostics {

struct EngineDiagnostics {
  std::uint64_t graph_version = 0;
  std::uint64_t processed_blocks = 0;
  std::uint64_t xrun_count = 0;
  double last_callback_seconds = 0.0;
  double peak_callback_seconds = 0.0;
};

}  // namespace sar::diagnostics

