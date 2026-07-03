#pragma once

#include <string>
#include <vector>

namespace sar::platform {

struct WindowsRealtimeThreadError {
  std::string code;
  std::string message;
};

class WindowsRealtimeThreadResult {
 public:
  static WindowsRealtimeThreadResult success();
  static WindowsRealtimeThreadResult failure(std::vector<WindowsRealtimeThreadError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<WindowsRealtimeThreadError>& errors() const noexcept;

 private:
  explicit WindowsRealtimeThreadResult(std::vector<WindowsRealtimeThreadError> errors);

  std::vector<WindowsRealtimeThreadError> errors_;
};

class WindowsRealtimeThreadScope {
 public:
  WindowsRealtimeThreadScope() = default;
  WindowsRealtimeThreadScope(const WindowsRealtimeThreadScope&) = delete;
  WindowsRealtimeThreadScope& operator=(const WindowsRealtimeThreadScope&) = delete;
  WindowsRealtimeThreadScope(WindowsRealtimeThreadScope&& other) noexcept;
  WindowsRealtimeThreadScope& operator=(WindowsRealtimeThreadScope&& other) noexcept;
  ~WindowsRealtimeThreadScope();

  [[nodiscard]] static WindowsRealtimeThreadResult enter_current_thread(
      WindowsRealtimeThreadScope& scope);

  void reset() noexcept;
  [[nodiscard]] bool active() const noexcept;

 private:
  explicit WindowsRealtimeThreadScope(void* avrt_handle) noexcept;

  void* avrt_handle_ = nullptr;
};

}  // namespace sar::platform
