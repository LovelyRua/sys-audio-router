#include "tools/virtual_asio_soak_options.h"

#include <charconv>
#include <iostream>
#include <limits>
#include <string_view>

namespace sar::tools {

namespace {

bool parse_u64(std::string_view text, std::uint64_t& value) {
  if (text.empty()) {
    return false;
  }
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parse_u32(std::string_view text, std::uint32_t& value) {
  std::uint64_t parsed = 0;
  if (!parse_u64(text, parsed) ||
      parsed > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  value = static_cast<std::uint32_t>(parsed);
  return true;
}

bool parse_block_sizes(std::string_view text,
                       std::vector<std::uint32_t>& values) {
  if (text.empty() || text.back() == ',') {
    return false;
  }
  std::vector<std::uint32_t> parsed;
  std::size_t begin = 0;
  while (begin < text.size()) {
    const auto comma = text.find(',', begin);
    const auto end = comma == std::string_view::npos ? text.size() : comma;
    std::uint32_t value = 0;
    if (!parse_u32(text.substr(begin, end - begin), value) || value < 16 ||
        value > 8192 || parsed.size() >= kVirtualAsioSoakMaximumClients) {
      return false;
    }
    parsed.push_back(value);
    begin = end + 1;
  }
  if (parsed.empty()) {
    return false;
  }
  values = std::move(parsed);
  return true;
}

std::wstring widen_ascii(std::string_view text) {
  return {text.begin(), text.end()};
}

}  // namespace

bool parse_virtual_asio_soak_options(int argc,
                                     char** argv,
                                     VirtualAsioSoakOptions& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help" || argument == "-h") {
      options.show_help = true;
      return true;
    }

    if (index + 1 >= argc) {
      std::cerr << "Missing value for " << argument << '\n';
      return false;
    }
    const std::string_view value(argv[++index]);
    auto parse_u32_value = [&](std::uint32_t& target) {
      if (!parse_u32(value, target)) {
        std::cerr << "Invalid integer value for " << argument << ": "
                  << value << '\n';
        return false;
      }
      return true;
    };
    auto parse_u64_value = [&](std::uint64_t& target) {
      if (!parse_u64(value, target)) {
        std::cerr << "Invalid integer value for " << argument << ": "
                  << value << '\n';
        return false;
      }
      return true;
    };

    if (argument == "--pipe") {
      if (value.empty() || value.size() > 240) {
        std::cerr << "Invalid pipe name\n";
        return false;
      }
      options.pipe_name = widen_ascii(value);
    } else if (argument == "--duration-ms") {
      if (!parse_u32_value(options.duration_ms)) return false;
    } else if (argument == "--clients") {
      if (!parse_u32_value(options.clients)) return false;
    } else if (argument == "--block-sizes") {
      if (!parse_block_sizes(value, options.block_sizes)) {
        std::cerr << "Invalid block-size list: " << value << '\n';
        return false;
      }
    } else if (argument == "--sample-rate") {
      if (!parse_u32_value(options.sample_rate)) return false;
    } else if (argument == "--channels") {
      if (!parse_u32_value(options.channels)) return false;
    } else if (argument == "--queue-blocks") {
      if (!parse_u32_value(options.queue_capacity_blocks)) return false;
    } else if (argument == "--wait-timeout-ms") {
      if (!parse_u32_value(options.wait_timeout_ms)) return false;
    } else if (argument == "--max-queue-failures") {
      if (!parse_u64_value(options.maximum_queue_failures)) return false;
    } else if (argument == "--max-dropouts") {
      if (!parse_u64_value(options.maximum_dropouts)) return false;
    } else if (argument == "--minimum-callback-percent") {
      if (!parse_u32_value(options.minimum_callback_percent)) return false;
    } else {
      std::cerr << "Unknown argument: " << argument << '\n';
      return false;
    }
  }

  if (options.duration_ms == 0 ||
      options.duration_ms > kVirtualAsioSoakMaximumDurationMs) {
    std::cerr << "duration-ms must be between 1 and "
              << kVirtualAsioSoakMaximumDurationMs << '\n';
    return false;
  }
  if (options.clients == 0 || options.clients > kVirtualAsioSoakMaximumClients) {
    std::cerr << "clients must be between 1 and "
              << kVirtualAsioSoakMaximumClients << '\n';
    return false;
  }
  if (options.sample_rate < 8'000 || options.sample_rate > 384'000 ||
      options.channels == 0 || options.channels > 64 ||
      options.queue_capacity_blocks < 2 || options.queue_capacity_blocks > 1024 ||
      options.wait_timeout_ms == 0 || options.wait_timeout_ms > 60'000 ||
      options.minimum_callback_percent > 100) {
    std::cerr << "One or more soak bounds are invalid\n";
    return false;
  }
  return true;
}

void print_virtual_asio_soak_usage() {
  std::cout
      << "Usage: sar_virtual_asio_soak [options]\n"
      << "  --pipe NAME                    Broker pipe name\n"
      << "  --duration-ms N                Run duration (1..86400000)\n"
      << "  --clients N                    Synthetic clients (1..32)\n"
      << "  --block-sizes A,B,C            Per-client rotating block sizes\n"
      << "  --sample-rate N                Shared sample rate (8000..384000)\n"
      << "  --channels N                   Input and output channels (1..64)\n"
      << "  --queue-blocks N               Queue capacity (2..1024)\n"
      << "  --wait-timeout-ms N            Per-callback output wait\n"
      << "  --max-queue-failures N         Accepted push/pop/signal failures\n"
      << "  --max-dropouts N               Accepted timeouts/discontinuities\n"
      << "  --minimum-callback-percent N   Minimum completed/expected (0..100)\n"
      << "  -h, --help                     Show this help\n";
}

}  // namespace sar::tools
