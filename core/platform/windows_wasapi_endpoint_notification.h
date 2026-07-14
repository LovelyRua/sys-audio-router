#pragma once

#include <atomic>
#include <cstdint>

namespace sar::platform {

struct WindowsWasapiEndpointNotificationTestAccess;

struct WasapiEndpointNotificationSnapshot {
  std::uint64_t capture_generation = 0;
  std::uint64_t render_generation = 0;
  bool event_reset_succeeded = false;
};

class WindowsWasapiEndpointNotification {
 public:
  WindowsWasapiEndpointNotification() = default;
  WindowsWasapiEndpointNotification(const WindowsWasapiEndpointNotification&) = delete;
  WindowsWasapiEndpointNotification& operator=(
      const WindowsWasapiEndpointNotification&) = delete;
  ~WindowsWasapiEndpointNotification();

  // The calling control thread must have initialized COM.
  [[nodiscard]] std::int32_t register_notifications() noexcept;
  [[nodiscard]] std::int32_t unregister_notifications() noexcept;

  [[nodiscard]] bool registered() const noexcept;
  [[nodiscard]] void* change_event() const noexcept;
  [[nodiscard]] bool reset_change_event() noexcept;
  [[nodiscard]] WasapiEndpointNotificationSnapshot consume_snapshot() noexcept;
  [[nodiscard]] std::uint64_t capture_generation() const noexcept;
  [[nodiscard]] std::uint64_t render_generation() const noexcept;

 private:
  friend struct WindowsWasapiEndpointNotificationTestAccess;

  [[nodiscard]] std::int32_t notify_default_device_for_test(
      std::int32_t data_flow) noexcept;
  [[nodiscard]] std::int32_t retain_failed_unregistration_for_test(
      std::int32_t unregister_result) noexcept;
  [[nodiscard]] std::int32_t finish_unregistration(
      std::int32_t unregister_result) noexcept;

  std::atomic<std::uint64_t> capture_generation_{0};
  std::atomic<std::uint64_t> render_generation_{0};
  void* change_event_ = nullptr;
  void* enumerator_ = nullptr;
  void* client_ = nullptr;
};

}  // namespace sar::platform
