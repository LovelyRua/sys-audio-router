#include "core/platform/windows_wasapi_endpoint_notification.h"

#include <mmdeviceapi.h>
#include <windows.h>

#include <iostream>

namespace sar::platform {

struct WindowsWasapiEndpointNotificationTestAccess {
  static std::int32_t notify_default_device(
      WindowsWasapiEndpointNotification& notification,
      EDataFlow flow) noexcept {
    return notification.notify_default_device_for_test(
        static_cast<std::int32_t>(flow));
  }

  static std::int32_t retain_failed_unregistration(
      WindowsWasapiEndpointNotification& notification,
      std::int32_t unregister_result) noexcept {
    return notification.retain_failed_unregistration_for_test(
        unregister_result);
  }
};

}  // namespace sar::platform

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
    std::cerr << "Failed to initialize COM: " << std::hex << com_result << '\n';
    return 1;
  }
  const bool uninitialize_com = SUCCEEDED(com_result);

  sar::platform::WindowsWasapiEndpointNotification notification;
  const auto unregistered_snapshot = notification.consume_snapshot();
  if (const auto failure = expect(
          unregistered_snapshot.capture_generation == 0 &&
              unregistered_snapshot.render_generation == 0 &&
              !unregistered_snapshot.event_reset_succeeded,
          "Unexpected snapshot for unregistered notification")) {
    return failure;
  }

  const auto register_result = notification.register_notifications();
  if (FAILED(register_result)) {
    std::cerr << "Failed to register endpoint notifications: " << std::hex
              << register_result << '\n';
    if (uninitialize_com) {
      CoUninitialize();
    }
    return 1;
  }

  if (const auto failure = expect(notification.registered(),
                                  "Expected notification registration")) {
    return failure;
  }
  if (const auto failure = expect(
          WaitForSingleObject(static_cast<HANDLE>(notification.change_event()), 0) ==
              WAIT_TIMEOUT,
          "Expected notification event to start nonsignaled")) {
    return failure;
  }

  using sar::platform::WindowsWasapiEndpointNotificationTestAccess;
  WindowsWasapiEndpointNotificationTestAccess::notify_default_device(notification,
                                                                     eCapture);
  if (const auto failure = expect(notification.capture_generation() == 1,
                                  "Expected capture generation increment")) {
    return failure;
  }
  if (const auto failure = expect(notification.render_generation() == 0,
                                  "Render generation changed for capture event")) {
    return failure;
  }
  if (const auto failure = expect(
          WaitForSingleObject(static_cast<HANDLE>(notification.change_event()), 0) ==
              WAIT_OBJECT_0,
          "Expected capture notification to signal event")) {
    return failure;
  }

  const auto capture_snapshot = notification.consume_snapshot();
  if (const auto failure = expect(
          capture_snapshot.capture_generation == 1 &&
              capture_snapshot.render_generation == 0 &&
              capture_snapshot.event_reset_succeeded,
          "Unexpected capture notification snapshot")) {
    return failure;
  }
  if (const auto failure = expect(
          WaitForSingleObject(static_cast<HANDLE>(notification.change_event()), 0) ==
              WAIT_TIMEOUT,
          "Snapshot did not clear notification event")) {
    return failure;
  }

  WindowsWasapiEndpointNotificationTestAccess::notify_default_device(notification,
                                                                     eRender);
  if (const auto failure = expect(notification.capture_generation() == 1,
                                  "Capture generation changed for render event")) {
    return failure;
  }
  if (const auto failure = expect(notification.render_generation() == 1,
                                  "Expected render generation increment")) {
    return failure;
  }

  WindowsWasapiEndpointNotificationTestAccess::notify_default_device(notification,
                                                                     eAll);
  if (const auto failure = expect(notification.capture_generation() == 2 &&
                                      notification.render_generation() == 2,
                                  "Expected eAll to increment both generations")) {
    return failure;
  }

  if (const auto failure = expect(notification.reset_change_event(),
                                  "Failed to reset notification event")) {
    return failure;
  }
  const auto retained_generation_snapshot = notification.consume_snapshot();
  if (const auto failure = expect(
          retained_generation_snapshot.capture_generation == 2 &&
              retained_generation_snapshot.render_generation == 2 &&
              retained_generation_snapshot.event_reset_succeeded,
          "Snapshot lost generations after event reset")) {
    return failure;
  }

  auto* const event_before_failed_unregister = notification.change_event();
  const auto injected_unregister_result =
      WindowsWasapiEndpointNotificationTestAccess::retain_failed_unregistration(
          notification, E_FAIL);
  if (const auto failure = expect(injected_unregister_result == E_FAIL,
                                  "Expected injected unregister failure")) {
    return failure;
  }
  if (const auto failure = expect(notification.registered(),
                                  "Failed unregister discarded registration")) {
    return failure;
  }
  if (const auto failure = expect(
          notification.change_event() == event_before_failed_unregister,
          "Failed unregister discarded notification event")) {
    return failure;
  }

  WindowsWasapiEndpointNotificationTestAccess::notify_default_device(notification,
                                                                     eCapture);
  if (const auto failure = expect(notification.capture_generation() == 3 &&
                                      notification.render_generation() == 2,
                                  "Callback state invalid after unregister failure")) {
    return failure;
  }
  if (const auto failure = expect(
          WaitForSingleObject(static_cast<HANDLE>(notification.change_event()), 0) ==
              WAIT_OBJECT_0,
          "Callback event invalid after unregister failure")) {
    return failure;
  }

  const auto unregister_result = notification.unregister_notifications();
  if (FAILED(unregister_result)) {
    std::cerr << "Failed to unregister endpoint notifications: " << std::hex
              << unregister_result << '\n';
    return 1;
  }
  if (const auto failure = expect(!notification.registered(),
                                  "Expected notification unregistration")) {
    return failure;
  }
  if (const auto failure = expect(notification.change_event() == nullptr,
                                  "Expected notification event cleanup")) {
    return failure;
  }
  if (const auto failure = expect(notification.capture_generation() == 3 &&
                                      notification.render_generation() == 2,
                                  "Expected generations to survive cleanup")) {
    return failure;
  }
  const auto cleaned_up_snapshot = notification.consume_snapshot();
  if (const auto failure = expect(
          cleaned_up_snapshot.capture_generation == 3 &&
              cleaned_up_snapshot.render_generation == 2 &&
              !cleaned_up_snapshot.event_reset_succeeded,
          "Unexpected snapshot after notification cleanup")) {
    return failure;
  }

  if (uninitialize_com) {
    CoUninitialize();
  }
  std::cout << "Windows WASAPI endpoint notification smoke test passed\n";
  return 0;
}
