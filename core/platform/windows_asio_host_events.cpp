#include "core/platform/windows_asio_host_events.h"

#include "third_party/asio_sdk_2.3.4/common/asio.h"

namespace sar::platform {

namespace {

constexpr std::uint32_t event_bit(WindowsAsioHostEvent event) noexcept {
  return static_cast<std::uint32_t>(event);
}

bool supported_event_selector(long selector) noexcept {
  switch (selector) {
    case kAsioResetRequest:
    case kAsioBufferSizeChange:
    case kAsioLatenciesChanged:
    case kAsioResyncRequest:
      return true;
    default:
      return false;
  }
}

}  // namespace

long WindowsAsioHostEvents::asio_message(long selector, long value) noexcept {
  if (selector == kAsioSelectorSupported) {
    return supported_event_selector(value) ? 1L : 0L;
  }

  switch (selector) {
    case kAsioResetRequest:
      publish(WindowsAsioHostEvent::ResetRequest, reset_requests_);
      return 1L;
    case kAsioBufferSizeChange:
      latest_buffer_size_.store(value, std::memory_order_relaxed);
      publish(WindowsAsioHostEvent::BufferSizeChange, buffer_size_changes_);
      return 1L;
    case kAsioLatenciesChanged:
      publish(WindowsAsioHostEvent::LatenciesChanged, latencies_changed_);
      return 1L;
    case kAsioResyncRequest:
      publish(WindowsAsioHostEvent::ResyncRequest, resync_requests_);
      return 1L;
    default:
      return 0L;
  }
}

WindowsAsioHostEventSnapshot WindowsAsioHostEvents::drain() noexcept {
  WindowsAsioHostEventSnapshot snapshot;
  snapshot.reset_requests = reset_requests_.exchange(0, std::memory_order_acq_rel);
  snapshot.buffer_size_changes =
      buffer_size_changes_.exchange(0, std::memory_order_acq_rel);
  snapshot.latencies_changed =
      latencies_changed_.exchange(0, std::memory_order_acq_rel);
  snapshot.resync_requests = resync_requests_.exchange(0, std::memory_order_acq_rel);
  snapshot.latest_buffer_size = latest_buffer_size_.load(std::memory_order_relaxed);
  if (snapshot.reset_requests != 0)
    snapshot.pending |= event_bit(WindowsAsioHostEvent::ResetRequest);
  if (snapshot.buffer_size_changes != 0)
    snapshot.pending |= event_bit(WindowsAsioHostEvent::BufferSizeChange);
  if (snapshot.latencies_changed != 0)
    snapshot.pending |= event_bit(WindowsAsioHostEvent::LatenciesChanged);
  if (snapshot.resync_requests != 0)
    snapshot.pending |= event_bit(WindowsAsioHostEvent::ResyncRequest);
  return snapshot;
}

void WindowsAsioHostEvents::publish(
    WindowsAsioHostEvent event, std::atomic<std::uint64_t>& counter) noexcept {
  (void)event;
  counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace sar::platform
