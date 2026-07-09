#include "tools/wasapi_measure_options.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace sar::tools {

namespace {

bool parse_u32(std::string_view text, std::uint32_t& value) {
  if (text.empty()) {
    return false;
  }

  std::uint64_t parsed = 0;
  for (const char ch : text) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    parsed = parsed * 10 + static_cast<std::uint64_t>(ch - '0');
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
  }

  value = static_cast<std::uint32_t>(parsed);
  return true;
}

}  // namespace

bool parse_wasapi_measure_options(int argc,
                                  char** argv,
                                  WasapiMeasureOptions& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--help" || arg == "-h") {
      options.show_help = true;
      return true;
    }

    auto parse_next = [&](std::uint32_t& target) {
      if (index + 1 >= argc) {
        std::cerr << "Missing value for " << arg << '\n';
        return false;
      }
      ++index;
      if (!parse_u32(argv[index], target)) {
        std::cerr << "Invalid integer value for " << arg << ": " << argv[index] << '\n';
        return false;
      }
      return true;
    };

    if (arg == "--duration-ms") {
      if (!parse_next(options.duration_ms)) {
        return false;
      }
    } else if (arg == "--timeout-ms") {
      if (!parse_next(options.timeout_ms)) {
        return false;
      }
    } else if (arg == "--require-healthy") {
      options.require_healthy = true;
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return false;
    }
  }

  return true;
}

}  // namespace sar::tools
