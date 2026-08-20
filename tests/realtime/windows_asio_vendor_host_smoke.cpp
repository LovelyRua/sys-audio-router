#include "core/platform/windows_asio_vendor_host.h"
#include "third_party/asio_sdk_2.3.4/common/asio.h"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace {
class MockDriver final : public sar::platform::WindowsAsioDriverLifecycle {
 public:
  long create_buffers(ASIOBufferInfo* infos, long count, long,
                      ASIOCallbacks* callbacks) noexcept override {
    calls.emplace_back("create"); callbacks_ = callbacks;
    for (long i = 0; i < count; ++i) { infos[i].buffers[0] = storage_[0]; infos[i].buffers[1] = storage_[1]; }
    return create_result;
  }
  long start() noexcept override { calls.emplace_back("start"); return start_result; }
  long stop() noexcept override { calls.emplace_back("stop"); return stop_result; }
  long dispose_buffers() noexcept override { calls.emplace_back("dispose"); return dispose_result; }
  void release() noexcept override { calls.emplace_back("release"); }
  long create_result = ASE_OK, start_result = ASE_OK, stop_result = ASE_OK, dispose_result = ASE_OK;
  std::vector<std::string> calls;
  ASIOCallbacks* callbacks_ = nullptr;
  float storage_[2][16]{};
};

bool process(void* context, const sar::realtime::AudioBuffer&,
             sar::realtime::AudioBuffer&) noexcept {
  ++*static_cast<int*>(context); return true;
}

sar::platform::WindowsAsioVendorHostConfig config(int* cycles) {
  return {16, {{0, true, sar::platform::WindowsAsioSampleEncoding::Float32Lsb},
               {0, false, sar::platform::WindowsAsioSampleEncoding::Float32Lsb}}, process, cycles};
}

void successful_lifecycle() {
  int cycles = 0; auto mock = std::make_unique<MockDriver>(); auto* observed = mock.get();
  sar::platform::WindowsAsioVendorHostResult result;
  auto host = sar::platform::WindowsAsioVendorHost::create(std::move(mock), config(&cycles), result);
  assert(host && result.ok());
  observed->callbacks_->bufferSwitch(0, ASIOFalse); assert(cycles == 1);
  assert(host->start().ok() && host->start().ok());
  assert(host->stop().ok() && host->stop().ok());
  assert(host->teardown().ok() && host->teardown().ok());
  assert((observed->calls == std::vector<std::string>{"create", "start", "stop", "dispose", "release"}));
}

void create_failure_releases_without_dispose() {
  int cycles = 0; auto mock = std::make_unique<MockDriver>();
  mock->create_result = ASE_InvalidMode; sar::platform::WindowsAsioVendorHostResult result;
  auto host = sar::platform::WindowsAsioVendorHost::create(std::move(mock), config(&cycles), result);
  assert(!host && result.error == sar::platform::WindowsAsioVendorHostError::CreateBuffersFailed);
}

void teardown_preserves_order_and_first_error() {
  int cycles = 0; auto mock = std::make_unique<MockDriver>(); auto* observed = mock.get();
  mock->stop_result = ASE_HWMalfunction; mock->dispose_result = ASE_InvalidMode;
  sar::platform::WindowsAsioVendorHostResult result;
  auto host = sar::platform::WindowsAsioVendorHost::create(std::move(mock), config(&cycles), result);
  assert(host && host->start().ok());
  assert(host->teardown().error == sar::platform::WindowsAsioVendorHostError::StopFailed);
  assert((observed->calls == std::vector<std::string>{"create", "start", "stop", "dispose", "release"}));
}
}  // namespace

int main() { successful_lifecycle(); create_failure_releases_without_dispose(); teardown_preserves_order_and_first_error(); }
