#include "core/platform/windows_wasapi_duplex_supervisor.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

using sar::platform::WasapiDuplexRuntime;
using sar::platform::WasapiDuplexRuntimeOpenResult;
using sar::platform::WasapiFailureClass;
using sar::platform::WasapiRealtimeWorkerError;
using sar::platform::WasapiRealtimeWorkerResult;
using sar::platform::WasapiRecoveryState;
using sar::platform::WindowsWasapiDuplexSupervisor;

struct RuntimeState {
  bool running = false;
  std::uint32_t start_count = 0;
  std::uint32_t stop_count = 0;
  std::vector<WasapiRealtimeWorkerError> start_errors;
  std::vector<WasapiRealtimeWorkerError> runtime_errors;
};

class ScriptedRuntime final : public WasapiDuplexRuntime {
 public:
  explicit ScriptedRuntime(std::shared_ptr<RuntimeState> state)
      : state_(std::move(state)) {}

  WasapiRealtimeWorkerResult start(std::uint32_t) override {
    ++state_->start_count;
    if (!state_->start_errors.empty()) {
      return WasapiRealtimeWorkerResult::failure(state_->start_errors);
    }
    state_->running = true;
    return WasapiRealtimeWorkerResult::success();
  }

  void stop() noexcept override {
    ++state_->stop_count;
    state_->running = false;
  }

  bool running() const noexcept override { return state_->running; }

  std::vector<WasapiRealtimeWorkerError> last_errors() const override {
    return state_->runtime_errors;
  }

 private:
  std::shared_ptr<RuntimeState> state_;
};

}  // namespace

int main() {
  const std::vector<WasapiRealtimeWorkerError> invalidated = {
      {"wasapi_device_lookup_failed", "device unavailable"}};
  const std::vector<WasapiRealtimeWorkerError> transient = {
      {"wasapi_render_buffer_failed", "render failed"}};
  const std::vector<WasapiRealtimeWorkerError> fatal = {
      {"graph_sample_rate_mismatch", "graph mismatch"}};
  const std::vector<WasapiRealtimeWorkerError> unknown = {
      {"future_error", "unknown"}};

  assert(sar::platform::classify_wasapi_failures(invalidated) ==
         WasapiFailureClass::DeviceInvalidated);
  assert(sar::platform::classify_wasapi_failures(transient) ==
         WasapiFailureClass::Transient);
  assert(sar::platform::classify_wasapi_failures(fatal) ==
         WasapiFailureClass::Fatal);
  assert(sar::platform::classify_wasapi_failures(unknown) ==
         WasapiFailureClass::Unknown);
  assert(sar::platform::classify_wasapi_failures({}) ==
         WasapiFailureClass::Unknown);

  auto first_runtime = std::make_shared<RuntimeState>();
  auto second_runtime = std::make_shared<RuntimeState>();
  std::uint32_t open_count = 0;
  WindowsWasapiDuplexSupervisor supervisor(
      [&]() -> WasapiDuplexRuntimeOpenResult {
        ++open_count;
        if (open_count == 1) {
          return WasapiDuplexRuntimeOpenResult::failure(invalidated);
        }
        auto state = open_count == 2 ? first_runtime : second_runtime;
        return WasapiDuplexRuntimeOpenResult::success(
            std::make_unique<ScriptedRuntime>(std::move(state)));
      },
      10);

  supervisor.start(100);
  assert(supervisor.state() == WasapiRecoveryState::Backoff);
  assert(supervisor.summary().attempt_count == 1);
  assert(supervisor.summary().next_attempt_at_ms == 100);
  assert(supervisor.summary().runtime_open_count == 1);
  assert(supervisor.summary().recovery_episode_count == 1);
  assert(supervisor.last_errors().size() == 1);

  supervisor.tick(100);
  assert(supervisor.running());
  assert(open_count == 2);
  assert(first_runtime->start_count == 1);
  assert(supervisor.last_errors().empty());
  assert(supervisor.summary().runtime_open_count == 2);
  assert(supervisor.summary().successful_recovery_count == 1);
  assert(supervisor.summary().last_recovery_duration_ms == 0);

  first_runtime->runtime_errors = transient;
  first_runtime->running = false;
  supervisor.tick(200);
  assert(supervisor.state() == WasapiRecoveryState::Backoff);
  assert(supervisor.summary().attempt_count == 2);
  assert(supervisor.summary().next_attempt_at_ms == 450);
  assert(first_runtime->stop_count == 1);
  assert(open_count == 2);
  assert(supervisor.summary().recovery_episode_count == 2);

  supervisor.tick(449);
  assert(supervisor.state() == WasapiRecoveryState::Backoff);
  supervisor.tick(450);
  assert(supervisor.running());
  assert(open_count == 3);
  assert(second_runtime->start_count == 1);
  assert(supervisor.summary().successful_recovery_count == 2);
  assert(supervisor.summary().last_recovery_duration_ms == 250);
  assert(supervisor.summary().maximum_recovery_duration_ms == 250);

  supervisor.stop(500);
  assert(supervisor.state() == WasapiRecoveryState::Stopped);
  assert(second_runtime->stop_count == 1);

  auto fatal_runtime = std::make_shared<RuntimeState>();
  fatal_runtime->start_errors = fatal;
  WindowsWasapiDuplexSupervisor fatal_supervisor(
      [fatal_runtime] {
        return WasapiDuplexRuntimeOpenResult::success(
            std::make_unique<ScriptedRuntime>(fatal_runtime));
      },
      10);
  fatal_supervisor.start(0);
  assert(fatal_supervisor.state() == WasapiRecoveryState::Faulted);
  assert(fatal_runtime->start_count == 1);
  assert(fatal_runtime->stop_count == 1);

  std::uint32_t failed_open_count = 0;
  WindowsWasapiDuplexSupervisor exhausted_supervisor(
      [&] {
        ++failed_open_count;
        return WasapiDuplexRuntimeOpenResult::failure(transient);
      },
      10);
  exhausted_supervisor.start(0);
  exhausted_supervisor.tick(0);
  exhausted_supervisor.tick(250);
  exhausted_supervisor.tick(1500);
  assert(exhausted_supervisor.state() == WasapiRecoveryState::Faulted);
  assert(failed_open_count == 4);
  assert(exhausted_supervisor.summary().runtime_open_count == 4);
  assert(exhausted_supervisor.summary().recovery_episode_count == 1);
  assert(exhausted_supervisor.summary().successful_recovery_count == 0);
  assert(exhausted_supervisor.summary().failed_recovery_count == 1);

  auto notification_first = std::make_shared<RuntimeState>();
  auto notification_second = std::make_shared<RuntimeState>();
  std::uint32_t notification_open_count = 0;
  WindowsWasapiDuplexSupervisor notification_supervisor(
      [&] {
        ++notification_open_count;
        auto state = notification_open_count == 1 ? notification_first
                                                   : notification_second;
        return WasapiDuplexRuntimeOpenResult::success(
            std::make_unique<ScriptedRuntime>(std::move(state)));
      },
      10);
  notification_supervisor.start(1000);
  assert(notification_supervisor.running());
  notification_supervisor.request_reopen(1100);
  assert(notification_supervisor.state() == WasapiRecoveryState::Backoff);
  assert(notification_first->stop_count == 1);
  assert(notification_supervisor.summary().recovery_episode_count == 1);
  notification_supervisor.request_reopen(1100);
  assert(notification_open_count == 1);
  notification_supervisor.tick(1100);
  assert(notification_supervisor.running());
  assert(notification_open_count == 2);
  assert(notification_second->start_count == 1);
  assert(notification_supervisor.summary().successful_recovery_count == 1);
}
