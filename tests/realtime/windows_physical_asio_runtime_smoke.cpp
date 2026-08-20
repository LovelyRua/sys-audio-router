#include "core/service/windows_physical_asio_runtime.h"

#include "core/graph/node.h"
#include "third_party/asio_sdk_2.3.4/common/asio.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace sar;

struct LifecycleState {
  long start_result = ASE_OK;
  int start_calls = 0;
  int stop_calls = 0;
  int dispose_calls = 0;
  int release_calls = 0;
  ASIOCallbacks* callbacks = nullptr;
  std::vector<ASIOBufferInfo> infos;
  std::vector<std::vector<float>> buffers;
};

class MockLifecycle final : public platform::WindowsAsioDriverLifecycle {
 public:
  explicit MockLifecycle(std::shared_ptr<LifecycleState> state)
      : state_(std::move(state)) {}

  long create_buffers(ASIOBufferInfo* infos, long count, long frames,
                      ASIOCallbacks* callbacks) noexcept override {
    state_->callbacks = callbacks;
    try {
      state_->infos.assign(infos, infos + count);
      state_->buffers.resize(static_cast<std::size_t>(count) * 2U);
      for (long index = 0; index < count; ++index) {
        for (std::size_t half = 0; half < 2; ++half) {
          auto& buffer = state_->buffers[static_cast<std::size_t>(index) * 2U + half];
          buffer.assign(static_cast<std::size_t>(frames), 0.0F);
          state_->infos[static_cast<std::size_t>(index)].buffers[half] =
              buffer.data();
          infos[index].buffers[half] = buffer.data();
        }
      }
    } catch (...) {
      return ASE_NoMemory;
    }
    return ASE_OK;
  }

  long start() noexcept override {
    ++state_->start_calls;
    if (state_->start_result == ASE_OK && state_->callbacks) {
      assert(state_->callbacks->asioMessage(kAsioSelectorSupported,
                                            kAsioResetRequest, nullptr,
                                            nullptr) == 1);
      assert(state_->callbacks->asioMessage(kAsioResetRequest, 0, nullptr,
                                            nullptr) == 1);
      state_->callbacks->bufferSwitch(0, ASIOFalse);
    }
    return state_->start_result;
  }
  long stop() noexcept override {
    ++state_->stop_calls;
    return ASE_OK;
  }
  long dispose_buffers() noexcept override {
    ++state_->dispose_calls;
    return ASE_OK;
  }
  void release() noexcept override { ++state_->release_calls; }

 private:
  std::shared_ptr<LifecycleState> state_;
};

class MockActivatedDriver final : public platform::WindowsAsioActivatedDriver {
 public:
  explicit MockActivatedDriver(std::shared_ptr<LifecycleState> lifecycle)
      : lifecycle_(std::move(lifecycle)) {}
  bool initialize(void*) noexcept override { return true; }
  bool can_sample_rate(double) noexcept override { return true; }
  bool set_sample_rate(double) noexcept override { return true; }
  bool channels(long&, long&) noexcept override { return false; }
  bool buffer_sizes(long&, long&, long&, long&) noexcept override { return false; }
  bool channel_info(long, bool, long&, std::string&) noexcept override {
    return false;
  }
  std::unique_ptr<platform::WindowsAsioDriverLifecycle> take_lifecycle()
      noexcept override {
    return std::make_unique<MockLifecycle>(lifecycle_);
  }

 private:
  std::shared_ptr<LifecycleState> lifecycle_;
};

class MockActivator final : public platform::WindowsAsioDriverActivator {
 public:
  explicit MockActivator(std::shared_ptr<LifecycleState> lifecycle)
      : lifecycle_(std::move(lifecycle)) {}
  std::unique_ptr<platform::WindowsAsioActivatedDriver> activate(
      const std::string&) noexcept override {
    return std::make_unique<MockActivatedDriver>(lifecycle_);
  }

 private:
  std::shared_ptr<LifecycleState> lifecycle_;
};

class FixedNegotiator final : public platform::WindowsAsioDriverNegotiator {
 public:
  platform::WindowsAsioNegotiationResult negotiate(
      platform::WindowsAsioActivatedDriver&,
      const platform::WindowsAsioControlOpenRequest&) noexcept override {
    return result;
  }
  platform::WindowsAsioNegotiationResult result;
};

platform::WindowsAsioNegotiatedChannel channel(std::uint32_t index,
                                                bool input) {
  return {index, input, platform::WindowsAsioSampleEncoding::Float32Lsb,
          input ? "Input" : "Output"};
}

platform::WindowsAsioControlOpenRequest request() {
  platform::WindowsAsioControlOpenRequest result;
  result.driver.clsid = "{runtime-smoke-driver}";
  result.sample_rate = 48000.0;
  result.preferred_block_frames = 16;
  return result;
}

std::unique_ptr<graph::Graph> graph_for(std::size_t channels) {
  auto result = std::make_unique<graph::Graph>(7, channels, 16, 48000);
  result->add_node(std::make_unique<graph::PassthroughNode>());
  return result;
}

void configure(FixedNegotiator& negotiator, std::size_t inputs,
               std::size_t outputs) {
  auto& config = negotiator.result.config;
  config.sample_rate = 48000.0;
  config.frames_per_block = 16;
  for (std::size_t index = 0; index < inputs; ++index) {
    config.inputs.push_back(channel(static_cast<std::uint32_t>(index), true));
  }
  for (std::size_t index = 0; index < outputs; ++index) {
    config.outputs.push_back(channel(static_cast<std::uint32_t>(index), false));
  }
}

void run_shape(std::size_t inputs, std::size_t outputs) {
  auto state = std::make_shared<LifecycleState>();
  MockActivator activator(state);
  FixedNegotiator negotiator;
  configure(negotiator, inputs, outputs);

  auto opened = service::WindowsPhysicalAsioRuntime::open(
      graph_for(std::max(inputs, outputs)), request(), activator, negotiator);
  assert(opened.ok());
  assert(opened.runtime->start() ==
         service::WindowsPhysicalAsioRuntimeError::None);
  assert(opened.runtime->stop() ==
         service::WindowsPhysicalAsioRuntimeError::None);
  assert(opened.runtime->stop() ==
         service::WindowsPhysicalAsioRuntimeError::None);

  const auto summary = opened.runtime->summary();
  assert(summary.state ==
         service::WindowsPhysicalAsioRuntimeState::Stopped);
  assert(summary.input_channels == inputs);
  assert(summary.output_channels == outputs);
  assert(summary.rejected_callbacks == 0);
  assert(summary.diagnostics_available);
  assert(summary.diagnostics.processed_blocks == 1);
  const auto events = opened.runtime->drain_host_events();
  assert(events.contains(platform::WindowsAsioHostEvent::ResetRequest));
  assert(events.reset_requests == 1);
  assert(opened.runtime->drain_host_events().empty());
  assert(state->start_calls == 1);
  assert(state->stop_calls == 1);
}

void start_failure_is_deterministic() {
  auto state = std::make_shared<LifecycleState>();
  state->start_result = ASE_HWMalfunction;
  MockActivator activator(state);
  FixedNegotiator negotiator;
  configure(negotiator, 1, 1);
  auto opened = service::WindowsPhysicalAsioRuntime::open(
      graph_for(1), request(), activator, negotiator);
  assert(opened.ok());
  assert(opened.runtime->start() ==
         service::WindowsPhysicalAsioRuntimeError::StartFailed);
  const auto summary = opened.runtime->summary();
  assert(summary.state == service::WindowsPhysicalAsioRuntimeState::Ready);
  assert(summary.last_error ==
         service::WindowsPhysicalAsioRuntimeError::StartFailed);
  assert(summary.vendor_host_error ==
         platform::WindowsAsioVendorHostError::StartFailed);
  assert(opened.runtime->stop() ==
         service::WindowsPhysicalAsioRuntimeError::None);
  assert(state->stop_calls == 0);
}

void graph_shape_mismatch_is_rejected_at_open() {
  auto state = std::make_shared<LifecycleState>();
  MockActivator activator(state);
  FixedNegotiator negotiator;
  configure(negotiator, 2, 1);
  auto opened = service::WindowsPhysicalAsioRuntime::open(
      graph_for(1), request(), activator, negotiator);
  assert(!opened.ok());
  assert(opened.error ==
         service::WindowsPhysicalAsioRuntimeError::GraphConfigurationMismatch);
}

}  // namespace

int main() {
  run_shape(0, 2);
  run_shape(2, 0);
  run_shape(2, 2);
  start_failure_is_deterministic();
  graph_shape_mismatch_is_rejected_at_open();
  assert(std::string(service::windows_physical_asio_runtime_error_name(
             service::WindowsPhysicalAsioRuntimeError::StopFailed)) ==
         "stop_failed");
}
