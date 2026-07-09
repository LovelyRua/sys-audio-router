#pragma once

#include <cstdint>

namespace sar::tools {

struct WasapiMeasureOptions {
  std::uint32_t duration_ms = 250;
  std::uint32_t timeout_ms = 10;
  bool require_healthy = false;
  bool show_help = false;
};

[[nodiscard]] bool parse_wasapi_measure_options(int argc,
                                                char** argv,
                                                WasapiMeasureOptions& options);

}  // namespace sar::tools
