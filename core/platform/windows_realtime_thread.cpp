#include "core/platform/windows_realtime_thread.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <avrt.h>

#include <cstdio>
#include <utility>

namespace sar::platform {

namespace {

std::string hresult_hex(DWORD result) {
  char buffer[16] = {};
  std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(result));
  return buffer;
}

}  // namespace

WindowsRealtimeThreadResult WindowsRealtimeThreadResult::success() {
  return WindowsRealtimeThreadResult({});
}

WindowsRealtimeThreadResult WindowsRealtimeThreadResult::failure(
    std::vector<WindowsRealtimeThreadError> errors) {
  return WindowsRealtimeThreadResult(std::move(errors));
}

bool WindowsRealtimeThreadResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<WindowsRealtimeThreadError>& WindowsRealtimeThreadResult::errors()
    const noexcept {
  return errors_;
}

WindowsRealtimeThreadResult::WindowsRealtimeThreadResult(
    std::vector<WindowsRealtimeThreadError> errors)
    : errors_(std::move(errors)) {}

WindowsRealtimeThreadScope::WindowsRealtimeThreadScope(
    WindowsRealtimeThreadScope&& other) noexcept
    : avrt_handle_(std::exchange(other.avrt_handle_, nullptr)) {}

WindowsRealtimeThreadScope& WindowsRealtimeThreadScope::operator=(
    WindowsRealtimeThreadScope&& other) noexcept {
  if (this != &other) {
    reset();
    avrt_handle_ = std::exchange(other.avrt_handle_, nullptr);
  }
  return *this;
}

WindowsRealtimeThreadScope::~WindowsRealtimeThreadScope() {
  reset();
}

WindowsRealtimeThreadResult WindowsRealtimeThreadScope::enter_current_thread(
    WindowsRealtimeThreadScope& scope) {
  scope.reset();

  DWORD task_index = 0;
  HANDLE avrt_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
  if (avrt_handle == nullptr) {
    const auto error = GetLastError();
    return WindowsRealtimeThreadResult::failure({
        {
            "mmcss_enter_failed",
            "MMCSS Pro Audio registration failed with " + hresult_hex(error) + ".",
        },
    });
  }

  if (AvSetMmThreadPriority(avrt_handle, AVRT_PRIORITY_CRITICAL) == FALSE) {
    const auto error = GetLastError();
    AvRevertMmThreadCharacteristics(avrt_handle);
    return WindowsRealtimeThreadResult::failure({
        {
            "mmcss_priority_failed",
            "MMCSS critical priority assignment failed with " + hresult_hex(error) + ".",
        },
    });
  }

  scope = WindowsRealtimeThreadScope(avrt_handle);
  return WindowsRealtimeThreadResult::success();
}

void WindowsRealtimeThreadScope::reset() noexcept {
  if (avrt_handle_ != nullptr) {
    AvRevertMmThreadCharacteristics(static_cast<HANDLE>(avrt_handle_));
    avrt_handle_ = nullptr;
  }
}

bool WindowsRealtimeThreadScope::active() const noexcept {
  return avrt_handle_ != nullptr;
}

WindowsRealtimeThreadScope::WindowsRealtimeThreadScope(void* avrt_handle) noexcept
    : avrt_handle_(avrt_handle) {}

}  // namespace sar::platform
