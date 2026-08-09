#include "tools/virtual_asio_soak_options.h"

#include "core/control/virtual_asio_broker_protocol.h"
#include "core/platform/windows_virtual_asio_events.h"
#include "core/platform/windows_virtual_asio_shared_queue.h"
#include "core/realtime/audio_buffer.h"
#include "core/service/windows_named_pipe_control.h"
#include "core/service/windows_virtual_asio_broker_client.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct ClientStats {
  std::uint64_t callback_attempts = 0;
  std::uint64_t callback_completions = 0;
  std::uint64_t queue_failures = 0;
  std::uint64_t dropouts = 0;
  std::uint64_t wait_timeouts = 0;
  std::uint64_t sequence_discontinuities = 0;
  std::uint64_t shutdowns = 0;
};

struct ClientRuntime {
  std::size_t index = 0;
  std::uint32_t frames = 0;
  std::unique_ptr<sar::service::WindowsVirtualAsioBrokerClient> client;
  sar::realtime::AudioBuffer input;
  sar::realtime::AudioBuffer output;
  ClientStats stats;
  std::thread worker;

  ClientRuntime(std::size_t client_index,
                std::uint32_t channels,
                std::uint32_t block_frames)
      : index(client_index),
        frames(block_frames),
        input(channels, block_frames),
        output(channels, block_frames) {}
};

std::uint64_t qpc_100ns() {
  LARGE_INTEGER counter{};
  LARGE_INTEGER frequency{};
  if (!QueryPerformanceCounter(&counter) ||
      !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
    return GetTickCount64() * 10'000ULL;
  }
  return static_cast<std::uint64_t>(
      (static_cast<long double>(counter.QuadPart) * 10'000'000.0L) /
      static_cast<long double>(frequency.QuadPart));
}

void fill_input(ClientRuntime& runtime) {
  const auto value = static_cast<float>(runtime.index + 1) * 0.01F;
  for (std::size_t channel = 0; channel < runtime.input.channels(); ++channel) {
    std::fill(runtime.input.channel(channel),
              runtime.input.channel(channel) + runtime.input.frames(), value);
  }
}

void run_client(ClientRuntime& runtime,
                const sar::tools::VirtualAsioSoakOptions& options,
                Clock::time_point start,
                Clock::time_point deadline,
                const std::atomic_bool& start_gate) {
  while (!start_gate.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  const auto period = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(
          static_cast<double>(runtime.frames) / options.sample_rate));
  auto next_callback = start;
  std::uint64_t sequence = 1;
  while (Clock::now() < deadline) {
    std::this_thread::sleep_until(next_callback);
    if (Clock::now() >= deadline) {
      break;
    }
    next_callback += period;
    ++runtime.stats.callback_attempts;

    const sar::platform::VirtualAsioSharedBlockMetadata sent{
        .sequence = sequence,
        .sample_position = sequence * runtime.frames,
        .qpc_position_100ns = qpc_100ns(),
    };
    ++sequence;
    if (runtime.client->push_input(runtime.input, sent) !=
        sar::platform::VirtualAsioSharedQueueStatus::Completed) {
      ++runtime.stats.queue_failures;
      ++runtime.stats.dropouts;
      continue;
    }
    if (!runtime.client->signal_input()) {
      ++runtime.stats.queue_failures;
      ++runtime.stats.dropouts;
      continue;
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - Clock::now());
    const auto wait_ms = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        remaining.count(), 1, options.wait_timeout_ms));
    const auto wait = runtime.client->wait_output_or_shutdown(wait_ms);
    if (wait.status ==
        sar::platform::WindowsVirtualAsioEventWaitStatus::TimedOut) {
      ++runtime.stats.wait_timeouts;
      ++runtime.stats.dropouts;
      continue;
    }
    if (wait.status != sar::platform::WindowsVirtualAsioEventWaitStatus::Ready) {
      ++runtime.stats.shutdowns;
      ++runtime.stats.dropouts;
      break;
    }

    sar::platform::VirtualAsioSharedBlockMetadata received;
    if (runtime.client->pop_output(runtime.output, received) !=
        sar::platform::VirtualAsioSharedQueueStatus::Completed) {
      ++runtime.stats.queue_failures;
      ++runtime.stats.dropouts;
      continue;
    }
    if (received.sequence != sent.sequence) {
      ++runtime.stats.sequence_discontinuities;
      ++runtime.stats.dropouts;
      continue;
    }
    ++runtime.stats.callback_completions;
  }
}

std::uint64_t expected_callbacks(const sar::tools::VirtualAsioSoakOptions& options,
                                 std::uint32_t frames) {
  return (static_cast<std::uint64_t>(options.duration_ms) *
          options.sample_rate) /
         (1000ULL * frames);
}

}  // namespace

int main(int argc, char** argv) {
  sar::tools::VirtualAsioSoakOptions options;
  if (!sar::tools::parse_virtual_asio_soak_options(argc, argv, options)) {
    sar::tools::print_virtual_asio_soak_usage();
    return 2;
  }
  if (options.show_help) {
    sar::tools::print_virtual_asio_soak_usage();
    return 0;
  }

  sar::service::NamedPipeControlConfig pipe_config;
  pipe_config.pipe_name = options.pipe_name;
  std::vector<std::unique_ptr<ClientRuntime>> clients;
  clients.reserve(options.clients);
  for (std::size_t index = 0; index < options.clients; ++index) {
    const auto frames = options.block_sizes[index % options.block_sizes.size()];
    auto runtime = std::make_unique<ClientRuntime>(
        index, options.channels, frames);
    fill_input(*runtime);
    const auto request_id = 0x534F414B00000000ULL + index + 1;
    auto connected = sar::service::WindowsVirtualAsioBrokerClient::connect(
        pipe_config,
        {
            .request_id = request_id,
            .client_id = "asio-soak-" + std::to_string(GetCurrentProcessId()) +
                         "-" + std::to_string(index),
            .format = {options.sample_rate, frames, options.channels,
                       options.channels},
            .queue_capacity_blocks = options.queue_capacity_blocks,
            .client_nonce_low = qpc_100ns() ^ request_id,
            .client_nonce_high = GetTickCount64() ^ (request_id << 1U),
        });
    if (!connected.ok()) {
      std::cerr << "asio_soak=connect_failed client=" << index
                << " frames=" << frames;
      for (const auto& error : connected.errors()) {
        std::cerr << " code=" << error.code << " message=\"" << error.message
                  << '"';
      }
      std::cerr << '\n';
      return 1;
    }
    runtime->client = connected.take_client();
    clients.push_back(std::move(runtime));
  }

  const auto start = Clock::now() + std::chrono::milliseconds(20);
  const auto deadline = start + std::chrono::milliseconds(options.duration_ms);
  std::atomic_bool start_gate = false;
  for (auto& runtime : clients) {
    runtime->worker = std::thread(run_client, std::ref(*runtime),
                                  std::cref(options), start, deadline,
                                  std::cref(start_gate));
  }
  start_gate.store(true, std::memory_order_release);
  for (auto& runtime : clients) {
    runtime->worker.join();
  }

  ClientStats total;
  bool healthy = true;
  for (auto& runtime : clients) {
    const auto expected = expected_callbacks(options, runtime->frames);
    const auto minimum = expected * options.minimum_callback_percent / 100;
    const auto& stats = runtime->stats;
    std::cout << "asio_soak=client index=" << runtime->index
              << " frames=" << runtime->frames
              << " callbacks=" << stats.callback_completions
              << " attempts=" << stats.callback_attempts
              << " expected=" << expected << " minimum=" << minimum
              << " queue_failures=" << stats.queue_failures
              << " dropouts=" << stats.dropouts
              << " wait_timeouts=" << stats.wait_timeouts
              << " discontinuities=" << stats.sequence_discontinuities
              << " shutdowns=" << stats.shutdowns << '\n';
    total.callback_attempts += stats.callback_attempts;
    total.callback_completions += stats.callback_completions;
    total.queue_failures += stats.queue_failures;
    total.dropouts += stats.dropouts;
    total.wait_timeouts += stats.wait_timeouts;
    total.sequence_discontinuities += stats.sequence_discontinuities;
    total.shutdowns += stats.shutdowns;
    healthy = healthy && stats.callback_completions >= minimum;
    static_cast<void>(runtime->client->disconnect(1000));
  }
  healthy = healthy &&
            total.queue_failures <= options.maximum_queue_failures &&
            total.dropouts <= options.maximum_dropouts;
  std::cout << "asio_soak=summary result=" << (healthy ? "passed" : "failed")
            << " clients=" << clients.size()
            << " callbacks=" << total.callback_completions
            << " attempts=" << total.callback_attempts
            << " queue_failures=" << total.queue_failures
            << " dropouts=" << total.dropouts
            << " wait_timeouts=" << total.wait_timeouts
            << " discontinuities=" << total.sequence_discontinuities
            << " shutdowns=" << total.shutdowns << '\n';
  return healthy ? 0 : 1;
}
