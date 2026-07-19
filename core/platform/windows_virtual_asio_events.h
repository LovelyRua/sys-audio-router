#pragma once

#include "core/platform/windows_virtual_asio_object_names.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sar::platform {

struct WindowsVirtualAsioEventError {
  std::string code;
  std::string message;
  std::uint32_t native_error = 0;
};

enum class WindowsVirtualAsioEventWaitStatus {
  Ready,
  Shutdown,
  TimedOut,
  Failed,
};

struct WindowsVirtualAsioEventWaitResult {
  WindowsVirtualAsioEventWaitStatus status =
      WindowsVirtualAsioEventWaitStatus::Failed;
  std::uint32_t native_error = 0;

  [[nodiscard]] bool ok() const noexcept {
    return status != WindowsVirtualAsioEventWaitStatus::Failed;
  }
};

class WindowsVirtualAsioEvents;

class WindowsVirtualAsioEventsOpenResult {
 public:
  static WindowsVirtualAsioEventsOpenResult success(
      std::unique_ptr<WindowsVirtualAsioEvents> events);
  static WindowsVirtualAsioEventsOpenResult failure(
      std::vector<WindowsVirtualAsioEventError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsVirtualAsioEvents& events() noexcept;
  [[nodiscard]] std::unique_ptr<WindowsVirtualAsioEvents> take_events() noexcept;
  [[nodiscard]] const std::vector<WindowsVirtualAsioEventError>& errors()
      const noexcept;

 private:
  WindowsVirtualAsioEventsOpenResult(
      std::unique_ptr<WindowsVirtualAsioEvents> events,
      std::vector<WindowsVirtualAsioEventError> errors);

  std::unique_ptr<WindowsVirtualAsioEvents> events_;
  std::vector<WindowsVirtualAsioEventError> errors_;
};

class WindowsVirtualAsioEvents {
 public:
  WindowsVirtualAsioEvents(const WindowsVirtualAsioEvents&) = delete;
  WindowsVirtualAsioEvents& operator=(const WindowsVirtualAsioEvents&) = delete;
  WindowsVirtualAsioEvents(WindowsVirtualAsioEvents&& other) noexcept;
  WindowsVirtualAsioEvents& operator=(WindowsVirtualAsioEvents&& other) noexcept;
  ~WindowsVirtualAsioEvents();

  [[nodiscard]] static WindowsVirtualAsioEventsOpenResult create(
      WindowsVirtualAsioObjectNames names);
  [[nodiscard]] static WindowsVirtualAsioEventsOpenResult open(
      WindowsVirtualAsioObjectNames names);

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool owner() const noexcept;
  [[nodiscard]] const WindowsVirtualAsioObjectNames& names() const noexcept;

  [[nodiscard]] bool signal_input() noexcept;
  [[nodiscard]] bool signal_output() noexcept;
  [[nodiscard]] bool signal_shutdown() noexcept;
  [[nodiscard]] bool reset_shutdown() noexcept;

  [[nodiscard]] WindowsVirtualAsioEventWaitResult wait_input_or_shutdown(
      std::uint32_t timeout_ms) noexcept;
  [[nodiscard]] WindowsVirtualAsioEventWaitResult wait_output_or_shutdown(
      std::uint32_t timeout_ms) noexcept;

  void close() noexcept;

 private:
  WindowsVirtualAsioEvents(WindowsVirtualAsioObjectNames names,
                           void* input_event,
                           void* output_event,
                           void* shutdown_event,
                           bool owner) noexcept;
  [[nodiscard]] WindowsVirtualAsioEventWaitResult wait_ready_or_shutdown(
      void* ready_event,
      std::uint32_t timeout_ms) noexcept;

  WindowsVirtualAsioObjectNames names_;
  void* input_event_ = nullptr;
  void* output_event_ = nullptr;
  void* shutdown_event_ = nullptr;
  bool owner_ = false;
};

}  // namespace sar::platform
