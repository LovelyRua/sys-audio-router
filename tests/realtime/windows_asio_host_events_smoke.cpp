#include "core/platform/windows_asio_host_events.h"

#include "third_party/asio_sdk_2.3.4/common/asio.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

}  // namespace

int main() {
  using sar::platform::WindowsAsioHostEvent;
  using sar::platform::WindowsAsioHostEvents;

  WindowsAsioHostEvents events;
  bool ok = true;
  for (const long selector : {kAsioResetRequest, kAsioBufferSizeChange,
                              kAsioLatenciesChanged, kAsioResyncRequest}) {
    ok &= expect(events.asio_message(kAsioSelectorSupported, selector) == 1,
                 "expected selector to be supported");
  }
  ok &= expect(events.asio_message(kAsioSelectorSupported, kAsioOverload) == 0,
               "unexpected selector support");
  ok &= expect(events.asio_message(0x7fffffffL, 0) == 0,
               "unknown event should be rejected");

  ok &= expect(events.asio_message(kAsioResetRequest, 0) == 1,
               "reset request was rejected");
  ok &= expect(events.asio_message(kAsioResetRequest, 0) == 1,
               "duplicate reset request was rejected");
  ok &= expect(events.asio_message(kAsioBufferSizeChange, 256) == 1,
               "buffer change was rejected");
  ok &= expect(events.asio_message(kAsioBufferSizeChange, 512) == 1,
               "newer buffer change was rejected");
  ok &= expect(events.asio_message(kAsioLatenciesChanged, 0) == 1,
               "latency change was rejected");
  ok &= expect(events.asio_message(kAsioResyncRequest, 0) == 1,
               "resync request was rejected");

  const auto coalesced = events.drain();
  ok &= expect(coalesced.contains(WindowsAsioHostEvent::ResetRequest),
               "reset flag missing");
  ok &= expect(coalesced.contains(WindowsAsioHostEvent::BufferSizeChange),
               "buffer flag missing");
  ok &= expect(coalesced.contains(WindowsAsioHostEvent::LatenciesChanged),
               "latency flag missing");
  ok &= expect(coalesced.contains(WindowsAsioHostEvent::ResyncRequest),
               "resync flag missing");
  ok &= expect(coalesced.reset_requests == 2 &&
                   coalesced.buffer_size_changes == 2 &&
                   coalesced.latencies_changed == 1 &&
                   coalesced.resync_requests == 1,
               "coalesced counts are incorrect");
  ok &= expect(coalesced.latest_buffer_size == 512,
               "latest buffer size was not retained");
  ok &= expect(events.drain().empty(), "drain did not clear pending events");

  constexpr std::uint32_t thread_count = 8;
  constexpr std::uint32_t requests_per_thread = 25000;
  std::vector<std::thread> producers;
  producers.reserve(thread_count);
  for (std::uint32_t thread = 0; thread < thread_count; ++thread) {
    producers.emplace_back([&events, thread] {
      for (std::uint32_t request = 0; request < requests_per_thread; ++request) {
        (void)events.asio_message(kAsioResetRequest, 0);
        (void)events.asio_message(kAsioResyncRequest, 0);
        (void)events.asio_message(kAsioBufferSizeChange,
                                  64L << (thread % 4));
      }
    });
  }
  for (auto& producer : producers) producer.join();

  const auto concurrent = events.drain();
  constexpr std::uint64_t expected =
      static_cast<std::uint64_t>(thread_count) * requests_per_thread;
  ok &= expect(concurrent.reset_requests == expected,
               "concurrent reset requests were lost");
  ok &= expect(concurrent.resync_requests == expected,
               "concurrent resync requests were lost");
  ok &= expect(concurrent.buffer_size_changes == expected,
               "concurrent buffer changes were lost");
  ok &= expect(concurrent.contains(WindowsAsioHostEvent::ResetRequest) &&
                   concurrent.contains(WindowsAsioHostEvent::ResyncRequest) &&
                   concurrent.contains(WindowsAsioHostEvent::BufferSizeChange),
               "concurrent pending flags were lost");
  ok &= expect(events.drain().empty(), "second concurrent drain was not empty");
  return ok ? 0 : 1;
}
