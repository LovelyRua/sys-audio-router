#pragma once

#include <cstdint>
#include <string>

namespace sar::tools {

struct PhysicalAsioMeasureOptions {
  std::string driver;
  std::uint32_t sample_rate = 48000;
  std::uint32_t block_frames = 0;
  std::uint32_t duration_ms = 5000;
  bool help = false;
};

struct PhysicalAsioMeasureOptionsResult {
  PhysicalAsioMeasureOptions options;
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

[[nodiscard]] PhysicalAsioMeasureOptionsResult parse_physical_asio_measure_options(
    int argc, const char* const* argv);

[[nodiscard]] const char* physical_asio_measure_usage() noexcept;

}  // namespace sar::tools
