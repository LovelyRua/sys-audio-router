#include "core/platform/windows_virtual_asio_events.h"
#include "core/platform/windows_virtual_asio_security.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <array>
#include <string_view>
#include <utility>

namespace sar::platform {
namespace {

constexpr std::wstring_view kObjectPrefix = L"Local\\SAR.VirtualASIO.v1.";
constexpr std::size_t kMaximumObjectNameCharacters = 240;

bool valid_object_name(std::wstring_view name,
                       std::wstring_view suffix) noexcept {
  return !name.empty() && name.size() <= kMaximumObjectNameCharacters &&
         name.starts_with(kObjectPrefix) && name.ends_with(suffix) &&
         name.find(L'\\', kObjectPrefix.size()) == std::wstring_view::npos &&
         name.find(L'/') == std::wstring_view::npos;
}

bool valid_names(const WindowsVirtualAsioObjectNames& names) noexcept {
  return valid_object_name(names.input_event, L".input-event") &&
         valid_object_name(names.output_event, L".output-event") &&
         valid_object_name(names.shutdown_event, L".shutdown-event") &&
         names.input_event != names.output_event &&
         names.input_event != names.shutdown_event &&
         names.output_event != names.shutdown_event;
}

void close_handle(void*& handle) noexcept {
  if (handle != nullptr) {
    CloseHandle(static_cast<HANDLE>(handle));
    handle = nullptr;
  }
}

WindowsVirtualAsioEventsOpenResult native_failure(std::string code,
                                                   std::string message,
                                                   DWORD error) {
  return WindowsVirtualAsioEventsOpenResult::failure({
      {std::move(code), std::move(message), error},
  });
}

}  // namespace

WindowsVirtualAsioEventsOpenResult WindowsVirtualAsioEventsOpenResult::success(
    std::unique_ptr<WindowsVirtualAsioEvents> events) {
  return {std::move(events), {}};
}

WindowsVirtualAsioEventsOpenResult WindowsVirtualAsioEventsOpenResult::failure(
    std::vector<WindowsVirtualAsioEventError> errors) {
  return {nullptr, std::move(errors)};
}

bool WindowsVirtualAsioEventsOpenResult::ok() const noexcept {
  return events_ != nullptr && errors_.empty();
}

WindowsVirtualAsioEvents& WindowsVirtualAsioEventsOpenResult::events() noexcept {
  return *events_;
}

std::unique_ptr<WindowsVirtualAsioEvents>
WindowsVirtualAsioEventsOpenResult::take_events() noexcept {
  return std::move(events_);
}

const std::vector<WindowsVirtualAsioEventError>&
WindowsVirtualAsioEventsOpenResult::errors() const noexcept {
  return errors_;
}

WindowsVirtualAsioEventsOpenResult::WindowsVirtualAsioEventsOpenResult(
    std::unique_ptr<WindowsVirtualAsioEvents> events,
    std::vector<WindowsVirtualAsioEventError> errors)
    : events_(std::move(events)), errors_(std::move(errors)) {}

WindowsVirtualAsioEvents::WindowsVirtualAsioEvents(
    WindowsVirtualAsioEvents&& other) noexcept
    : names_(std::move(other.names_)),
      input_event_(std::exchange(other.input_event_, nullptr)),
      output_event_(std::exchange(other.output_event_, nullptr)),
      shutdown_event_(std::exchange(other.shutdown_event_, nullptr)),
      owner_(std::exchange(other.owner_, false)) {}

WindowsVirtualAsioEvents& WindowsVirtualAsioEvents::operator=(
    WindowsVirtualAsioEvents&& other) noexcept {
  if (this != &other) {
    close();
    names_ = std::move(other.names_);
    input_event_ = std::exchange(other.input_event_, nullptr);
    output_event_ = std::exchange(other.output_event_, nullptr);
    shutdown_event_ = std::exchange(other.shutdown_event_, nullptr);
    owner_ = std::exchange(other.owner_, false);
  }
  return *this;
}

WindowsVirtualAsioEvents::~WindowsVirtualAsioEvents() {
  close();
}

WindowsVirtualAsioEventsOpenResult WindowsVirtualAsioEvents::create(
    WindowsVirtualAsioObjectNames names) {
  if (!valid_names(names)) {
    return WindowsVirtualAsioEventsOpenResult::failure({
        {"invalid_virtual_asio_event_names",
         "Virtual ASIO events require bounded Local namespace names.", 0},
    });
  }

  auto security_result =
      WindowsVirtualAsioSecurityAttributes::create_for_current_user();
  if (!security_result.ok()) {
    const auto& error = security_result.errors().front();
    return WindowsVirtualAsioEventsOpenResult::failure({
        {error.code, error.message, error.native_error},
    });
  }
  auto security = security_result.take_attributes();
  auto* attributes = static_cast<SECURITY_ATTRIBUTES*>(
      security->native_attributes());

  HANDLE input =
      CreateEventW(attributes, FALSE, FALSE, names.input_event.c_str());
  const auto input_error = GetLastError();
  if (input == nullptr) {
    return native_failure("virtual_asio_input_event_create_failed",
                          "Could not create Virtual ASIO input event.",
                          input_error);
  }
  if (input_error == ERROR_ALREADY_EXISTS) {
    CloseHandle(input);
    return native_failure("virtual_asio_event_already_exists",
                          "Virtual ASIO event name is already in use.",
                          input_error);
  }

  HANDLE output =
      CreateEventW(attributes, FALSE, FALSE, names.output_event.c_str());
  const auto output_error = GetLastError();
  if (output == nullptr || output_error == ERROR_ALREADY_EXISTS) {
    const auto error = output == nullptr ? output_error : ERROR_ALREADY_EXISTS;
    if (output != nullptr) {
      CloseHandle(output);
    }
    CloseHandle(input);
    return native_failure(output == nullptr
                              ? "virtual_asio_output_event_create_failed"
                              : "virtual_asio_event_already_exists",
                          "Could not exclusively create Virtual ASIO output event.",
                          error);
  }

  HANDLE shutdown =
      CreateEventW(attributes, TRUE, FALSE, names.shutdown_event.c_str());
  const auto shutdown_error = GetLastError();
  if (shutdown == nullptr || shutdown_error == ERROR_ALREADY_EXISTS) {
    const auto error =
        shutdown == nullptr ? shutdown_error : ERROR_ALREADY_EXISTS;
    if (shutdown != nullptr) {
      CloseHandle(shutdown);
    }
    CloseHandle(output);
    CloseHandle(input);
    return native_failure(shutdown == nullptr
                              ? "virtual_asio_shutdown_event_create_failed"
                              : "virtual_asio_event_already_exists",
                          "Could not exclusively create Virtual ASIO shutdown event.",
                          error);
  }

  return WindowsVirtualAsioEventsOpenResult::success(
      std::unique_ptr<WindowsVirtualAsioEvents>(new WindowsVirtualAsioEvents(
          std::move(names), input, output, shutdown, true)));
}

WindowsVirtualAsioEventsOpenResult WindowsVirtualAsioEvents::open(
    WindowsVirtualAsioObjectNames names) {
  if (!valid_names(names)) {
    return WindowsVirtualAsioEventsOpenResult::failure({
        {"invalid_virtual_asio_event_names",
         "Virtual ASIO events require bounded Local namespace names.", 0},
    });
  }
  constexpr DWORD access = SYNCHRONIZE | EVENT_MODIFY_STATE;
  HANDLE input = OpenEventW(access, FALSE, names.input_event.c_str());
  if (input == nullptr) {
    return native_failure("virtual_asio_input_event_open_failed",
                          "Could not open Virtual ASIO input event.",
                          GetLastError());
  }
  HANDLE output = OpenEventW(access, FALSE, names.output_event.c_str());
  if (output == nullptr) {
    const auto error = GetLastError();
    CloseHandle(input);
    return native_failure("virtual_asio_output_event_open_failed",
                          "Could not open Virtual ASIO output event.", error);
  }
  HANDLE shutdown = OpenEventW(access, FALSE, names.shutdown_event.c_str());
  if (shutdown == nullptr) {
    const auto error = GetLastError();
    CloseHandle(output);
    CloseHandle(input);
    return native_failure("virtual_asio_shutdown_event_open_failed",
                          "Could not open Virtual ASIO shutdown event.", error);
  }
  return WindowsVirtualAsioEventsOpenResult::success(
      std::unique_ptr<WindowsVirtualAsioEvents>(new WindowsVirtualAsioEvents(
          std::move(names), input, output, shutdown, false)));
}

bool WindowsVirtualAsioEvents::valid() const noexcept {
  return input_event_ != nullptr && output_event_ != nullptr &&
         shutdown_event_ != nullptr;
}

bool WindowsVirtualAsioEvents::owner() const noexcept {
  return owner_;
}

const WindowsVirtualAsioObjectNames& WindowsVirtualAsioEvents::names()
    const noexcept {
  return names_;
}

bool WindowsVirtualAsioEvents::signal_input() noexcept {
  return input_event_ != nullptr && SetEvent(static_cast<HANDLE>(input_event_));
}

bool WindowsVirtualAsioEvents::signal_output() noexcept {
  return output_event_ != nullptr && SetEvent(static_cast<HANDLE>(output_event_));
}

bool WindowsVirtualAsioEvents::signal_shutdown() noexcept {
  return shutdown_event_ != nullptr &&
         SetEvent(static_cast<HANDLE>(shutdown_event_));
}

bool WindowsVirtualAsioEvents::reset_shutdown() noexcept {
  return shutdown_event_ != nullptr &&
         ResetEvent(static_cast<HANDLE>(shutdown_event_));
}

WindowsVirtualAsioEventWaitResult
WindowsVirtualAsioEvents::wait_input_or_shutdown(
    std::uint32_t timeout_ms) noexcept {
  return wait_ready_or_shutdown(input_event_, timeout_ms);
}

WindowsVirtualAsioEventWaitResult
WindowsVirtualAsioEvents::wait_output_or_shutdown(
    std::uint32_t timeout_ms) noexcept {
  return wait_ready_or_shutdown(output_event_, timeout_ms);
}

void WindowsVirtualAsioEvents::close() noexcept {
  close_handle(shutdown_event_);
  close_handle(output_event_);
  close_handle(input_event_);
  owner_ = false;
}

WindowsVirtualAsioEvents::WindowsVirtualAsioEvents(
    WindowsVirtualAsioObjectNames names,
    void* input_event,
    void* output_event,
    void* shutdown_event,
    bool owner) noexcept
    : names_(std::move(names)),
      input_event_(input_event),
      output_event_(output_event),
      shutdown_event_(shutdown_event),
      owner_(owner) {}

WindowsVirtualAsioEventWaitResult
WindowsVirtualAsioEvents::wait_ready_or_shutdown(
    void* ready_event,
    std::uint32_t timeout_ms) noexcept {
  if (ready_event == nullptr || shutdown_event_ == nullptr) {
    return {WindowsVirtualAsioEventWaitStatus::Failed, ERROR_INVALID_HANDLE};
  }
  const std::array<HANDLE, 2> handles{
      static_cast<HANDLE>(shutdown_event_),
      static_cast<HANDLE>(ready_event),
  };
  const auto result = WaitForMultipleObjects(
      static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout_ms);
  if (result == WAIT_OBJECT_0) {
    return {WindowsVirtualAsioEventWaitStatus::Shutdown, 0};
  }
  if (result == WAIT_OBJECT_0 + 1) {
    return {WindowsVirtualAsioEventWaitStatus::Ready, 0};
  }
  if (result == WAIT_TIMEOUT) {
    return {WindowsVirtualAsioEventWaitStatus::TimedOut, 0};
  }
  return {WindowsVirtualAsioEventWaitStatus::Failed, GetLastError()};
}

}  // namespace sar::platform
