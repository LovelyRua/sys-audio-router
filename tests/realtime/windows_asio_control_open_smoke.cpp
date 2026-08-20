#include "core/platform/windows_asio_control_open.h"

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <array>
#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace sar::platform;

bool graph(void*, const sar::realtime::AudioBuffer&, sar::realtime::AudioBuffer&) noexcept {
  return true;
}

struct LifecycleState {
  long create_result = ASE_OK;
  int create_calls = 0;
  int releases = 0;
};

class MockLifecycle final : public WindowsAsioDriverLifecycle {
 public:
  explicit MockLifecycle(std::shared_ptr<LifecycleState> state) : state_(std::move(state)) {}
  long create_buffers(ASIOBufferInfo* infos, long count, long frames,
                      ASIOCallbacks*) noexcept override {
    ++state_->create_calls;
    if (state_->create_result != ASE_OK) return state_->create_result;
    storage_.resize(static_cast<std::size_t>(count) * 2);
    for (long i = 0; i < count; ++i) {
      storage_[static_cast<std::size_t>(i) * 2].resize(static_cast<std::size_t>(frames));
      storage_[static_cast<std::size_t>(i) * 2 + 1].resize(static_cast<std::size_t>(frames));
      infos[i].buffers[0] = storage_[static_cast<std::size_t>(i) * 2].data();
      infos[i].buffers[1] = storage_[static_cast<std::size_t>(i) * 2 + 1].data();
    }
    return ASE_OK;
  }
  long start() noexcept override { return ASE_OK; }
  long stop() noexcept override { return ASE_OK; }
  long dispose_buffers() noexcept override { return ASE_OK; }
  void release() noexcept override { ++state_->releases; }
 private:
  std::shared_ptr<LifecycleState> state_;
  std::vector<std::vector<float>> storage_;
};

struct DriverState {
  bool initialize = true;
  bool can_rate = true;
  bool set_rate = true;
  bool channels_ok = true;
  bool buffers_ok = true;
  long inputs = 2;
  long outputs = 2;
  long minimum = 64;
  long maximum = 512;
  long preferred = 128;
  long granularity = -1;
  long sample_type = ASIOSTFloat32LSB;
  double observed_rate = 0.0;
  std::shared_ptr<LifecycleState> lifecycle = std::make_shared<LifecycleState>();
};

class MockDriver final : public WindowsAsioActivatedDriver {
 public:
  explicit MockDriver(std::shared_ptr<DriverState> state) : state_(std::move(state)) {}
  bool initialize(void*) noexcept override { return state_->initialize; }
  bool can_sample_rate(double rate) noexcept override {
    state_->observed_rate = rate;
    return state_->can_rate;
  }
  bool set_sample_rate(double rate) noexcept override {
    state_->observed_rate = rate;
    return state_->set_rate;
  }
  bool channels(long& inputs, long& outputs) noexcept override {
    inputs = state_->inputs;
    outputs = state_->outputs;
    return state_->channels_ok;
  }
  bool buffer_sizes(long& minimum, long& maximum, long& preferred,
                    long& granularity) noexcept override {
    minimum = state_->minimum;
    maximum = state_->maximum;
    preferred = state_->preferred;
    granularity = state_->granularity;
    return state_->buffers_ok;
  }
  bool channel_info(long channel, bool input, long& type,
                    std::string& name) noexcept override {
    type = state_->sample_type;
    name = std::string(input ? "Input " : "Output ") + std::to_string(channel + 1);
    return true;
  }
  std::unique_ptr<WindowsAsioDriverLifecycle> take_lifecycle() noexcept override {
    return std::make_unique<MockLifecycle>(state_->lifecycle);
  }
 private:
  std::shared_ptr<DriverState> state_;
};

class MockActivator final : public WindowsAsioDriverActivator {
 public:
  explicit MockActivator(std::shared_ptr<DriverState> state) : state_(std::move(state)) {}
  std::unique_ptr<WindowsAsioActivatedDriver> activate(const std::string& clsid) noexcept override {
    observed_clsid = clsid;
    if (fail) return {};
    return std::make_unique<MockDriver>(state_);
  }
  bool fail = false;
  std::string observed_clsid;
 private:
  std::shared_ptr<DriverState> state_;
};

class FixedNegotiator final : public WindowsAsioDriverNegotiator {
 public:
  WindowsAsioNegotiationResult negotiate(WindowsAsioActivatedDriver&,
                                          const WindowsAsioControlOpenRequest&) noexcept override {
    return result;
  }
  WindowsAsioNegotiationResult result;
};

WindowsAsioControlOpenRequest request() {
  WindowsAsioControlOpenRequest value;
  value.driver.clsid = "{12345678-1234-1234-1234-123456789abc}";
  value.sample_rate = 48000.0;
  value.preferred_block_frames = 256;
  value.graph_process = graph;
  return value;
}

}  // namespace

int main() {
  auto state = std::make_shared<DriverState>();
  MockActivator activator(state);
  auto negotiator = make_windows_asio_driver_negotiator();
  assert(negotiator);

  auto opened = open_windows_asio_control(request(), activator, *negotiator);
  assert(opened.ok());
  assert(activator.observed_clsid == request().driver.clsid);
  assert(state->observed_rate == 48000.0);
  assert(opened.config.frames_per_block == 256);
  assert(opened.config.inputs.size() == 2);
  assert(opened.config.outputs.size() == 2);
  assert(opened.config.inputs[0].name == "Input 1");
  assert(opened.config.outputs[1].name == "Output 2");
  assert(state->lifecycle->create_calls == 1);
  opened.host.reset();
  assert(state->lifecycle->releases == 1);

  activator.fail = true;
  auto activation_failed = open_windows_asio_control(request(), activator, *negotiator);
  assert(activation_failed.error == WindowsAsioControlOpenError::ActivationFailed);
  activator.fail = false;

  state->can_rate = false;
  auto unsupported_rate = open_windows_asio_control(request(), activator, *negotiator);
  assert(unsupported_rate.error == WindowsAsioControlOpenError::SampleRateUnsupported);
  state->can_rate = true;

  auto bad_block_request = request();
  bad_block_request.preferred_block_frames = 192;
  auto bad_block = open_windows_asio_control(bad_block_request, activator, *negotiator);
  assert(bad_block.error == WindowsAsioControlOpenError::BufferSizeUnsupported);

  state->sample_type = ASIOSTFloat32MSB;
  auto unsupported_encoding = open_windows_asio_control(request(), activator, *negotiator);
  assert(unsupported_encoding.error == WindowsAsioControlOpenError::SampleEncodingUnsupported);
  state->sample_type = ASIOSTFloat32LSB;

  FixedNegotiator fixed;
  fixed.result.config.sample_rate = 48000.0;
  fixed.result.config.frames_per_block = 128;
  fixed.result.config.inputs.push_back({0, true, WindowsAsioSampleEncoding::Float32Lsb,
                                        "Injected input"});
  auto injected = open_windows_asio_control(request(), activator, fixed);
  assert(injected.ok());
  assert(injected.config.inputs[0].name == "Injected input");
  injected.host.reset();

  state->lifecycle->create_result = ASE_InvalidParameter;
  auto host_failed = open_windows_asio_control(request(), activator, fixed);
  assert(host_failed.error == WindowsAsioControlOpenError::VendorHostCreationFailed);
  assert(std::string(windows_asio_control_open_error_name(host_failed.error)) ==
         "vendor_host_creation_failed");
}
