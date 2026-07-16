#include "core/service/windows_named_pipe_control.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sar::service {

namespace {

constexpr std::uint32_t kFrameHeaderBytes = 4;

std::wstring full_pipe_name(const std::wstring& pipe_name) {
  constexpr wchar_t kPrefix[] = L"\\\\.\\pipe\\";
  if (pipe_name.starts_with(kPrefix)) {
    return pipe_name;
  }
  return std::wstring{kPrefix} + pipe_name;
}

NamedPipeControlError win32_error(std::string code,
                                  std::string message,
                                  DWORD native_code) {
  return {std::move(code), std::move(message), native_code};
}

std::array<std::byte, kFrameHeaderBytes> encode_length(std::uint32_t length) noexcept {
  return {
      static_cast<std::byte>(length & 0xFFU),
      static_cast<std::byte>((length >> 8U) & 0xFFU),
      static_cast<std::byte>((length >> 16U) & 0xFFU),
      static_cast<std::byte>((length >> 24U) & 0xFFU),
  };
}

std::uint32_t decode_length(
    const std::array<std::byte, kFrameHeaderBytes>& header) noexcept {
  return std::to_integer<std::uint32_t>(header[0]) |
         (std::to_integer<std::uint32_t>(header[1]) << 8U) |
         (std::to_integer<std::uint32_t>(header[2]) << 16U) |
         (std::to_integer<std::uint32_t>(header[3]) << 24U);
}

bool read_exact(HANDLE pipe, std::span<std::byte> destination, DWORD& error) noexcept {
  std::size_t offset = 0;
  while (offset < destination.size()) {
    DWORD transferred = 0;
    const auto remaining = destination.size() - offset;
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
        remaining, std::numeric_limits<DWORD>::max()));
    if (!ReadFile(pipe, destination.data() + offset, chunk, &transferred, nullptr)) {
      error = GetLastError();
      return false;
    }
    if (transferred == 0) {
      error = ERROR_BROKEN_PIPE;
      return false;
    }
    offset += transferred;
  }
  error = ERROR_SUCCESS;
  return true;
}

bool write_exact(HANDLE pipe,
                 std::span<const std::byte> source,
                 DWORD& error) noexcept {
  std::size_t offset = 0;
  while (offset < source.size()) {
    DWORD transferred = 0;
    const auto remaining = source.size() - offset;
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
        remaining, std::numeric_limits<DWORD>::max()));
    if (!WriteFile(pipe, source.data() + offset, chunk, &transferred, nullptr)) {
      error = GetLastError();
      return false;
    }
    if (transferred == 0) {
      error = ERROR_BROKEN_PIPE;
      return false;
    }
    offset += transferred;
  }
  error = ERROR_SUCCESS;
  return true;
}

bool valid_config(const NamedPipeControlConfig& config) noexcept {
  return !config.pipe_name.empty() && config.maximum_message_bytes != 0;
}

void close_pipe(HANDLE pipe) noexcept {
  if (pipe != INVALID_HANDLE_VALUE) {
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }
}

}  // namespace

NamedPipeControlResult NamedPipeControlResult::success(std::vector<std::byte> payload) {
  return {std::move(payload), {}, true};
}

NamedPipeControlResult NamedPipeControlResult::failure(NamedPipeControlError error) {
  return {{}, std::move(error), false};
}

bool NamedPipeControlResult::ok() const noexcept { return succeeded_; }

const std::vector<std::byte>& NamedPipeControlResult::payload() const noexcept {
  return payload_;
}

std::vector<std::byte> NamedPipeControlResult::take_payload() noexcept {
  return std::move(payload_);
}

const NamedPipeControlError& NamedPipeControlResult::error() const noexcept {
  return error_;
}

NamedPipeControlResult::NamedPipeControlResult(std::vector<std::byte> payload,
                                               NamedPipeControlError error,
                                               bool succeeded) noexcept
    : payload_(std::move(payload)),
      error_(std::move(error)),
      succeeded_(succeeded) {}

WindowsNamedPipeControlServer::WindowsNamedPipeControlServer(
    NamedPipeControlConfig config,
    NamedPipeControlHandler handler)
    : config_(std::move(config)), handler_(std::move(handler)) {
  if (!handler_) {
    throw std::invalid_argument("Named pipe control server requires a handler");
  }
}

WindowsNamedPipeControlServer::~WindowsNamedPipeControlServer() { stop(); }

NamedPipeControlResult WindowsNamedPipeControlServer::start() {
  if (!valid_config(config_)) {
    return NamedPipeControlResult::failure(
        {"invalid_pipe_config", "Pipe name and maximum message size are required.", 0});
  }
  if (running_.load(std::memory_order_acquire)) {
    return NamedPipeControlResult::failure(
        {"pipe_server_already_running", "Named pipe control server is already running.", 0});
  }
  if (thread_.joinable()) {
    thread_.join();
  }

  stop_requested_.store(false, std::memory_order_release);
  startup_complete_.store(false, std::memory_order_release);
  startup_succeeded_.store(false, std::memory_order_release);
  accepted_connections_.store(0, std::memory_order_relaxed);
  completed_requests_.store(0, std::memory_order_relaxed);
  protocol_errors_.store(0, std::memory_order_relaxed);
  handler_errors_.store(0, std::memory_order_relaxed);
  while (error_lock_.test_and_set(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  last_errors_.clear();
  error_lock_.clear(std::memory_order_release);

  running_.store(true, std::memory_order_release);
  thread_ = std::thread([this] { run(); });
  constexpr auto kStartupWait = std::chrono::seconds(2);
  const auto deadline = std::chrono::steady_clock::now() + kStartupWait;
  while (!startup_complete_.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  if (!startup_complete_.load(std::memory_order_acquire)) {
    stop();
    return NamedPipeControlResult::failure(
        {"pipe_server_start_timeout", "Named pipe control server startup timed out.", 0});
  }
  if (!startup_succeeded_.load(std::memory_order_acquire)) {
    if (thread_.joinable()) {
      thread_.join();
    }
    const auto errors = last_errors();
    if (!errors.empty()) {
      return NamedPipeControlResult::failure(errors.front());
    }
    return NamedPipeControlResult::failure(
        {"pipe_server_start_failed", "Named pipe control server failed to start.", 0});
  }
  return NamedPipeControlResult::success();
}

void WindowsNamedPipeControlServer::stop() noexcept {
  stop_requested_.store(true, std::memory_order_release);
  if (thread_.joinable()) {
    const auto path = full_pipe_name(config_.pipe_name);
    HANDLE wake = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (wake != INVALID_HANDLE_VALUE) {
      CloseHandle(wake);
    }
    thread_.join();
  }
  running_.store(false, std::memory_order_release);
}

bool WindowsNamedPipeControlServer::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

NamedPipeControlStats WindowsNamedPipeControlServer::stats() const noexcept {
  return {
      accepted_connections_.load(std::memory_order_relaxed),
      completed_requests_.load(std::memory_order_relaxed),
      protocol_errors_.load(std::memory_order_relaxed),
      handler_errors_.load(std::memory_order_relaxed),
  };
}

std::vector<NamedPipeControlError> WindowsNamedPipeControlServer::last_errors() const {
  while (error_lock_.test_and_set(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  auto result = last_errors_;
  error_lock_.clear(std::memory_order_release);
  return result;
}

void WindowsNamedPipeControlServer::run() noexcept {
  const auto path = full_pipe_name(config_.pipe_name);
  bool startup_published = false;

  while (!stop_requested_.load(std::memory_order_acquire)) {
    HANDLE pipe = CreateNamedPipeW(
        path.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
        config_.maximum_message_bytes + kFrameHeaderBytes,
        config_.maximum_message_bytes + kFrameHeaderBytes, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      const auto native = GetLastError();
      while (error_lock_.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      last_errors_.push_back(win32_error(
          "pipe_create_failed", "CreateNamedPipeW failed.", native));
      error_lock_.clear(std::memory_order_release);
      startup_succeeded_.store(false, std::memory_order_release);
      startup_complete_.store(true, std::memory_order_release);
      running_.store(false, std::memory_order_release);
      return;
    }

    if (!startup_published) {
      startup_succeeded_.store(true, std::memory_order_release);
      startup_complete_.store(true, std::memory_order_release);
      startup_published = true;
    }

    const BOOL connected = ConnectNamedPipe(pipe, nullptr)
                               ? TRUE
                               : GetLastError() == ERROR_PIPE_CONNECTED;
    if (!connected) {
      CloseHandle(pipe);
      if (!stop_requested_.load(std::memory_order_acquire)) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      }
      continue;
    }
    accepted_connections_.fetch_add(1, std::memory_order_relaxed);
    if (stop_requested_.load(std::memory_order_acquire)) {
      close_pipe(pipe);
      break;
    }

    std::array<std::byte, kFrameHeaderBytes> header{};
    DWORD native = ERROR_SUCCESS;
    if (!read_exact(pipe, header, native)) {
      if (native != ERROR_BROKEN_PIPE && native != ERROR_NO_DATA) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      }
      close_pipe(pipe);
      continue;
    }
    const auto request_length = decode_length(header);
    if (request_length > config_.maximum_message_bytes) {
      protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      close_pipe(pipe);
      continue;
    }

    std::vector<std::byte> request(request_length);
    if (!read_exact(pipe, request, native)) {
      protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      close_pipe(pipe);
      continue;
    }

    NamedPipeControlResult response = NamedPipeControlResult::failure(
        {"pipe_handler_failed", "Named pipe control handler failed.", 0});
    try {
      response = handler_(request);
    } catch (const std::exception& error) {
      response = NamedPipeControlResult::failure(
          {"pipe_handler_exception", error.what(), 0});
    } catch (...) {
      response = NamedPipeControlResult::failure(
          {"pipe_handler_exception", "Named pipe handler threw an unknown exception.", 0});
    }
    if (!response.ok()) {
      handler_errors_.fetch_add(1, std::memory_order_relaxed);
      close_pipe(pipe);
      continue;
    }
    if (response.payload().size() > config_.maximum_message_bytes) {
      handler_errors_.fetch_add(1, std::memory_order_relaxed);
      close_pipe(pipe);
      continue;
    }

    const auto response_header =
        encode_length(static_cast<std::uint32_t>(response.payload().size()));
    if (!write_exact(pipe, response_header, native) ||
        !write_exact(pipe, response.payload(), native)) {
      protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      close_pipe(pipe);
      continue;
    }
    completed_requests_.fetch_add(1, std::memory_order_relaxed);
    close_pipe(pipe);
  }

  running_.store(false, std::memory_order_release);
  if (!startup_published) {
    startup_succeeded_.store(false, std::memory_order_release);
    startup_complete_.store(true, std::memory_order_release);
  }
}

NamedPipeControlResult transact_named_pipe_control(
    const NamedPipeControlConfig& config,
    std::span<const std::byte> request,
    std::uint32_t timeout_ms) {
  if (!valid_config(config)) {
    return NamedPipeControlResult::failure(
        {"invalid_pipe_config", "Pipe name and maximum message size are required.", 0});
  }
  if (request.size() > config.maximum_message_bytes) {
    return NamedPipeControlResult::failure(
        {"pipe_request_too_large", "Named pipe request exceeds the configured limit.", 0});
  }

  const auto path = full_pipe_name(config.pipe_name);
  if (!WaitNamedPipeW(path.c_str(), timeout_ms)) {
    return NamedPipeControlResult::failure(win32_error(
        "pipe_wait_failed", "WaitNamedPipeW failed.", GetLastError()));
  }
  HANDLE pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    return NamedPipeControlResult::failure(win32_error(
        "pipe_connect_failed", "CreateFileW could not connect to the pipe.",
        GetLastError()));
  }

  DWORD native = ERROR_SUCCESS;
  const auto request_header =
      encode_length(static_cast<std::uint32_t>(request.size()));
  if (!write_exact(pipe, request_header, native) ||
      !write_exact(pipe, request, native)) {
    CloseHandle(pipe);
    return NamedPipeControlResult::failure(win32_error(
        "pipe_write_failed", "Writing the named pipe request failed.", native));
  }

  std::array<std::byte, kFrameHeaderBytes> response_header{};
  if (!read_exact(pipe, response_header, native)) {
    CloseHandle(pipe);
    return NamedPipeControlResult::failure(win32_error(
        "pipe_read_failed", "Reading the named pipe response header failed.", native));
  }
  const auto response_length = decode_length(response_header);
  if (response_length > config.maximum_message_bytes) {
    CloseHandle(pipe);
    return NamedPipeControlResult::failure(
        {"pipe_response_too_large", "Named pipe response exceeds the configured limit.", 0});
  }
  std::vector<std::byte> response(response_length);
  if (!read_exact(pipe, response, native)) {
    CloseHandle(pipe);
    return NamedPipeControlResult::failure(win32_error(
        "pipe_read_failed", "Reading the named pipe response failed.", native));
  }
  CloseHandle(pipe);
  return NamedPipeControlResult::success(std::move(response));
}

}  // namespace sar::service
