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
  // Wait timeout counters. Incremented by the realtime worker or loop wrappers
  // when a capture or render stream handle wait exceeds its deadline without
  // being signaled. These are separate from xrun_count because a wait timeout
  // does not necessarily mean the graph missed its deadline — it may indicate
  // device stall, driver reconfiguration, or clock drift.
  std::uint64_t capture_wait_timeout_cycles = 0;
  std::uint64_t render_wait_timeout_cycles = 0;
  // Sample conversion failure counters. Incremented by loop wrappers when
  // import_interleaved_to_float or export_float_to_interleaved returns a
  // non-Ok status. These surface format mismatches, buffer sizing bugs, and
  // channel-count contract violations without requiring log I/O on the hot
  // path.
  std::uint64_t sample_conversion_import_failures = 0;
  std::uint64_t sample_conversion_export_failures = 0;
  double last_callback_seconds = 0.0;
  double peak_callback_seconds = 0.0;
};

}  // namespace sar::diagnostics
