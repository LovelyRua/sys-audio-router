#include "core/platform/windows_wasapi_duplex_supervisor.h"
#include "core/platform/windows_wasapi_endpoint_notification.h"

#include <cassert>
#include <cstdint>
#include <mmdeviceapi.h>
#include <memory>
#include <utility>
#include <vector>
#include <windows.h>

namespace sar::platform {

struct WindowsWasapiEndpointNotificationTestAccess {
  static std::int32_t notify_default_device(
      WindowsWasapiEndpointNotification& notification,
      EDataFlow flow) noexcept {
    return notification.notify_default_device_for_test(
        static_cast<std::int32_t>(flow), static_cast<std::int32_t>(eConsole));
  }
};

}  // namespace sar::platform

namespace {

using sar::platform::WasapiDuplexRuntime;
using sar::platform::WasapiDuplexRuntimeEndpoints;
using sar::platform::WasapiDuplexRuntimeOpenResult;
using sar::platform::WasapiFailureClass;
using sar::platform::WasapiEndpointSelection;
using sar::platform::WasapiEndpointSelectionPolicy;
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
  const std::vector<WasapiRealtimeWorkerError> native_invalidated = {{
      "wasapi_render_buffer_failed",
      "render device invalidated",
      static_cast<std::int32_t>(0x88890004U),
  }};
  const std::vector<WasapiRealtimeWorkerError> transient = {
      {"wasapi_render_buffer_failed", "render failed"}};
  const std::vector<WasapiRealtimeWorkerError> fatal = {
      {"graph_sample_rate_mismatch", "graph mismatch"}};
  const std::vector<WasapiRealtimeWorkerError> unknown = {
      {"future_error", "unknown"}};

  assert(sar::platform::classify_wasapi_failures(invalidated) ==
         WasapiFailureClass::DeviceInvalidated);
  assert(sar::platform::classify_wasapi_failures(native_invalidated) ==
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
  assert(supervisor.summary().next_attempt_at_ms == 700);
  assert(first_runtime->stop_count == 1);
  assert(open_count == 2);
  assert(supervisor.summary().recovery_episode_count == 2);

  supervisor.tick(699);
  assert(supervisor.state() == WasapiRecoveryState::Backoff);
  supervisor.tick(700);
  assert(supervisor.running());
  assert(open_count == 3);
  assert(second_runtime->start_count == 1);
  assert(supervisor.summary().successful_recovery_count == 2);
  assert(supervisor.summary().last_recovery_duration_ms == 500);
  assert(supervisor.summary().maximum_recovery_duration_ms == 500);

  supervisor.stop(800);
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

  auto delayed_runtime = std::make_shared<RuntimeState>();
  std::uint32_t delayed_open_count = 0;
  WindowsWasapiDuplexSupervisor delayed_supervisor(
      [&] {
        ++delayed_open_count;
        if (delayed_open_count < 4) {
          return WasapiDuplexRuntimeOpenResult::failure(transient);
        }
        return WasapiDuplexRuntimeOpenResult::success(
            std::make_unique<ScriptedRuntime>(delayed_runtime));
      },
      10);
  delayed_supervisor.start(0);
  delayed_supervisor.tick(0);
  delayed_supervisor.tick(500);
  delayed_supervisor.tick(3499);
  assert(delayed_supervisor.state() == WasapiRecoveryState::Backoff);
  delayed_supervisor.tick(3500);
  assert(delayed_supervisor.running());
  assert(delayed_open_count == 4);
  assert(delayed_supervisor.summary().successful_recovery_count == 1);
  assert(delayed_supervisor.summary().last_recovery_duration_ms == 3500);
  delayed_supervisor.stop(3600);

  std::uint32_t failed_open_count = 0;
  WindowsWasapiDuplexSupervisor exhausted_supervisor(
      [&] {
        ++failed_open_count;
        return WasapiDuplexRuntimeOpenResult::failure(transient);
      },
      10);
  exhausted_supervisor.start(0);
  exhausted_supervisor.tick(0);
  exhausted_supervisor.tick(500);
  exhausted_supervisor.tick(3500);
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

  auto identity_first = std::make_shared<RuntimeState>();
  auto identity_second = std::make_shared<RuntimeState>();
  std::uint32_t identity_open_count = 0;
  WindowsWasapiDuplexSupervisor identity_supervisor(
      [&] {
        ++identity_open_count;
        const bool first = identity_open_count == 1;
        return WasapiDuplexRuntimeOpenResult::success(
            std::make_unique<ScriptedRuntime>(first ? identity_first
                                                    : identity_second),
            WasapiDuplexRuntimeEndpoints{
                .capture_device_id = first ? "capture-a" : "capture-b",
                .render_device_id = first ? "render-a" : "render-b",
            });
      },
      10);
  identity_supervisor.start(1500);
  assert(identity_supervisor.summary().active_capture_device_id == "capture-a");
  assert(identity_supervisor.summary().active_render_device_id == "render-a");
  identity_supervisor.request_reopen(1600);
  assert(identity_supervisor.summary().active_capture_device_id.empty());
  assert(identity_supervisor.summary().active_render_device_id.empty());
  identity_supervisor.tick(1600);
  assert(identity_supervisor.running());
  assert(identity_supervisor.summary().active_capture_device_id == "capture-b");
  assert(identity_supervisor.summary().active_render_device_id == "render-b");
  assert(identity_supervisor.summary().runtime_open_count == 2);
  assert(identity_supervisor.summary().successful_recovery_count == 1);
  identity_supervisor.stop(1700);

  const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  assert(SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE);
  const bool uninitialize_com = SUCCEEDED(com_result);

  sar::platform::WindowsWasapiEndpointNotification endpoint_notifications;
  assert(SUCCEEDED(endpoint_notifications.register_notifications()));

  auto default_first = std::make_shared<RuntimeState>();
  auto default_second = std::make_shared<RuntimeState>();
  std::uint32_t default_open_count = 0;
  WindowsWasapiDuplexSupervisor default_supervisor(
      [&] {
        ++default_open_count;
        auto state = default_open_count == 1 ? default_first : default_second;
        return WasapiDuplexRuntimeOpenResult::success(
            std::make_unique<ScriptedRuntime>(std::move(state)));
      },
      10);
  default_supervisor.start(2000);
  auto requirements =
      default_supervisor.poll_endpoint_notifications(endpoint_notifications,
                                                     2000);
  assert(!requirements.capture && !requirements.render);
  assert(default_supervisor.summary().endpoint_generations_initialized);
  assert(default_supervisor.summary()
             .endpoint_notification_reset_failure_count == 0);

  using sar::platform::WindowsWasapiEndpointNotificationTestAccess;
  assert(SUCCEEDED(
      WindowsWasapiEndpointNotificationTestAccess::notify_default_device(
          endpoint_notifications, eCapture)));
  requirements = default_supervisor.poll_endpoint_notifications(
      endpoint_notifications, 2100);
  assert(requirements.capture && !requirements.render);
  assert(default_supervisor.state() == WasapiRecoveryState::Backoff);
  assert(default_first->stop_count == 1);
  assert(default_supervisor.summary().capture_endpoint_generation == 1);
  assert(default_supervisor.summary().endpoint_notification_reopen_count == 1);
  default_supervisor.tick(2100);
  assert(default_supervisor.running());
  assert(default_open_count == 2);

  const WasapiEndpointSelectionPolicy pinned_capture_policy(
      WasapiEndpointSelection::pinned_device_id("capture-pinned"),
      WasapiEndpointSelection::follow_default());
  auto pinned_runtime = std::make_shared<RuntimeState>();
  WindowsWasapiDuplexSupervisor pinned_supervisor(
      [pinned_runtime] {
        return WasapiDuplexRuntimeOpenResult::success(
            std::make_unique<ScriptedRuntime>(pinned_runtime));
      },
      10,
      pinned_capture_policy);
  pinned_supervisor.start(3000);
  requirements = pinned_supervisor.poll_endpoint_notifications(
      endpoint_notifications, 3000);
  assert(!requirements.capture && !requirements.render);
  assert(SUCCEEDED(
      WindowsWasapiEndpointNotificationTestAccess::notify_default_device(
          endpoint_notifications, eCapture)));
  requirements = pinned_supervisor.poll_endpoint_notifications(
      endpoint_notifications, 3100);
  assert(!requirements.capture && !requirements.render);
  assert(pinned_supervisor.running());
  assert(pinned_runtime->stop_count == 0);
  assert(pinned_supervisor.summary().endpoint_notification_reopen_count == 0);

  pinned_supervisor.stop(3200);
  default_supervisor.stop(3200);
  assert(SUCCEEDED(endpoint_notifications.unregister_notifications()));
  if (uninitialize_com) {
    CoUninitialize();
  }
}
