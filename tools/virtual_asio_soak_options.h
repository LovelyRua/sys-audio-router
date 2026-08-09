#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sar::tools {

inline constexpr std::uint32_t kVirtualAsioSoakMaximumClients = 32;
inline constexpr std::uint32_t kVirtualAsioSoakMaximumDurationMs =
    24U * 60U * 60U * 1000U;

struct VirtualAsioSoakOptions {
  std::wstring pipe_name = L"sys-audio-route-virtual-asio";
  std::uint32_t duration_ms = 60'000;
  std::uint32_t clients = 4;
  std::vector<std::uint32_t> block_sizes{64, 128, 256};
  std::uint32_t sample_rate = 48'000;
  std::uint32_t channels = 2;
  std::uint32_t queue_capacity_blocks = 8;
  std::uint32_t wait_timeout_ms = 250;
  std::uint64_t maximum_queue_failures = 0;
  std::uint64_t maximum_dropouts = 0;
  std::uint32_t minimum_callback_percent = 80;
  bool show_help = false;
};

[[nodiscard]] bool parse_virtual_asio_soak_options(
    int argc, char** argv, VirtualAsioSoakOptions& options);

void print_virtual_asio_soak_usage();

}  // namespace sar::tools
