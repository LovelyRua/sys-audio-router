#pragma once

#include <atomic>
#include <cstdint>

namespace sar::platform {

enum class WindowsAsioHostEvent : std::uint32_t {
  None = 0,
  ResetRequest = 1u << 0,
  BufferSizeChange = 1u << 1,
  LatenciesChanged = 1u << 2,
  ResyncRequest = 1u << 3,
};

struct WindowsAsioHostEventSnapshot {
  std::uint32_t pending = 0;
  std::uint64_t reset_requests = 0;
  std::uint64_t buffer_size_changes = 0;
  std::uint64_t latencies_changed = 0;
  std::uint64_t resync_requests = 0;
  long latest_buffer_size = 0;

  [[nodiscard]] bool empty() const noexcept { return pending == 0; }
  [[nodiscard]] bool contains(WindowsAsioHostEvent event) const noexcept {
    return (pending & static_cast<std::uint32_t>(event)) != 0;
  }
};

class WindowsAsioHostEvents {
 public:
  // ASIO callback entry point. This only publishes atomic state.
  [[nodiscard]] long asio_message(long selector, long value) noexcept;

  // Control-plane entry point. Repeated requests are coalesced in pending while
  // their exact occurrence counts remain available for diagnostics.
  [[nodiscard]] WindowsAsioHostEventSnapshot drain() noexcept;

 private:
  void publish(WindowsAsioHostEvent event,
               std::atomic<std::uint64_t>& counter) noexcept;

  std::atomic<std::uint64_t> reset_requests_{0};
  std::atomic<std::uint64_t> buffer_size_changes_{0};
  std::atomic<std::uint64_t> latencies_changed_{0};
  std::atomic<std::uint64_t> resync_requests_{0};
  std::atomic<long> latest_buffer_size_{0};
};

}  // namespace sar::platform
