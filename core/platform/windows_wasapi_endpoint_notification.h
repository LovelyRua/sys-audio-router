#pragma once

#include <atomic>
#include <cstdint>

namespace sar::platform {

struct WindowsWasapiEndpointNotificationTestAccess;

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
  [[nodiscard]] std::uint64_t capture_generation() const noexcept;
  [[nodiscard]] std::uint64_t render_generation() const noexcept;

 private:
  friend struct WindowsWasapiEndpointNotificationTestAccess;

  [[nodiscard]] std::int32_t notify_default_device_for_test(
      std::int32_t data_flow) noexcept;

  std::atomic<std::uint64_t> capture_generation_{0};
  std::atomic<std::uint64_t> render_generation_{0};
  void* change_event_ = nullptr;
  void* enumerator_ = nullptr;
  void* client_ = nullptr;
};

}  // namespace sar::platform
