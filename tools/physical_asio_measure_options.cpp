#include "tools/physical_asio_measure_options.h"

#include <charconv>
#include <limits>
#include <string_view>

namespace sar::tools {
namespace {

bool parse_positive_u32(std::string_view text, std::uint32_t& value) noexcept {
  if (text.empty()) {
    return false;
  }
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      parsed == 0 || parsed > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  value = static_cast<std::uint32_t>(parsed);
  return true;
}

}  // namespace

PhysicalAsioMeasureOptionsResult parse_physical_asio_measure_options(
    int argc, const char* const* argv) {
  PhysicalAsioMeasureOptionsResult result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index] ? argv[index] : "";
    if (argument == "--help" || argument == "-h") {
      result.options.help = true;
      continue;
    }
    if (argument != "--driver" && argument != "--sample-rate" &&
        argument != "--block-frames" && argument != "--duration-ms") {
      result.error = "unknown argument: " + std::string(argument);
      return result;
    }
    if (++index >= argc || !argv[index] || std::string_view(argv[index]).empty()) {
      result.error = "missing value for " + std::string(argument);
      return result;
    }
    const std::string_view value = argv[index];
    if (argument == "--driver") {
      result.options.driver = value;
      continue;
    }
    std::uint32_t parsed = 0;
    if (!parse_positive_u32(value, parsed)) {
      result.error = "invalid positive integer for " + std::string(argument);
      return result;
    }
    if (argument == "--sample-rate") result.options.sample_rate = parsed;
    if (argument == "--block-frames") result.options.block_frames = parsed;
    if (argument == "--duration-ms") result.options.duration_ms = parsed;
  }
  if (!result.options.help && result.options.driver.empty()) {
    result.error = "--driver is required";
  }
  return result;
}

const char* physical_asio_measure_usage() noexcept {
  return
      "Usage: sar_measure_physical_asio --driver NAME-OR-CLSID [options]\n"
      "  --sample-rate HZ     Requested sample rate (default: 48000)\n"
      "  --block-frames N     Requested ASIO block size (default: driver preferred)\n"
      "  --duration-ms MS     Measurement duration (default: 5000)\n"
      "  -h, --help           Show this help\n";
}

}  // namespace sar::tools
