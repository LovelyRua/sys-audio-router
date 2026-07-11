#pragma once

#include <cstdint>

namespace sar::diagnostics {

struct EngineDiagnostics {
  std::uint64_t graph_version = 0;
  std::uint64_t processed_blocks = 0;
  std::uint64_t xrun_count = 0;
  std::uint64_t capture_fifo_fill_frames = 0;
  std::uint64_t render_fifo_fill_frames = 0;
  std::uint64_t capture_fifo_overflow_cycles = 0;
  std::uint64_t capture_fifo_overflow_frames = 0;
  std::uint64_t render_fifo_overflow_cycles = 0;
  std::uint64_t render_fifo_overflow_frames = 0;
  std::uint64_t render_fifo_underflow_cycles = 0;
  std::uint64_t render_fifo_underflow_frames = 0;
  double last_callback_seconds = 0.0;
  double peak_callback_seconds = 0.0;
};

}  // namespace sar::diagnostics
