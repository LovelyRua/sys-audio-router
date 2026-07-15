#pragma once

#include <cstdint>
#include <string>

namespace sar::tools {

struct WasapiMeasureOptions {
  std::uint32_t duration_ms = 250;
  std::uint32_t timeout_ms = 10;
  std::string capture_device_id;
  std::string render_device_id;
  bool require_healthy = false;
  bool show_help = false;
};

[[nodiscard]] bool parse_wasapi_measure_options(int argc,
                                                char** argv,
                                                WasapiMeasureOptions& options,
                                                bool allow_endpoint_selection = false);

}  // namespace sar::tools
