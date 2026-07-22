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
  std::uint64_t virtual_asio_pushed_blocks = 0;
  std::uint64_t virtual_asio_dropped_blocks = 0;
  std::uint64_t virtual_asio_consumed_blocks = 0;
  std::uint64_t virtual_asio_mixed_blocks = 0;
  std::uint64_t virtual_asio_silent_reads = 0;
  std::uint64_t virtual_asio_clipped_samples = 0;
  std::uint64_t virtual_asio_non_finite_samples = 0;
  std::uint64_t virtual_asio_maximum_queue_depth = 0;
  std::uint64_t virtual_asio_active_producers = 0;
  double last_callback_seconds = 0.0;
  double peak_callback_seconds = 0.0;
  double virtual_asio_peak = 0.0;
};

}  // namespace sar::diagnostics
