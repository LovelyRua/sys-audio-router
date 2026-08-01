#include "core/service/windows_named_pipe_control.h"

#include "core/platform/windows_virtual_asio_security.h"

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
constexpr std::size_t kMaximumClientWorkers = 16;

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

enum class PipeIoStatus {
  Completed,
  Cancelled,
  TimedOut,
  Failed,
};

using PipeDeadline = std::chrono::steady_clock::time_point;

DWORD remaining_timeout_ms(PipeDeadline deadline) noexcept {
  if (deadline == PipeDeadline::max()) {
    return INFINITE;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline) {
    return 0;
  }
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - now);
  if (remaining.count() >= static_cast<std::int64_t>(INFINITE - 1U)) {
    return INFINITE - 1U;
  }
  return static_cast<DWORD>(std::max<std::int64_t>(1, remaining.count()));
}

PipeIoStatus wait_for_pipe_io(HANDLE pipe,
                              OVERLAPPED& overlapped,
                              HANDLE stop_event,
                              PipeDeadline deadline,
                              DWORD& transferred,
                              DWORD& error) noexcept {
  std::array<HANDLE, 2> handles{overlapped.hEvent, stop_event};
  const DWORD handle_count = stop_event == nullptr ? 1U : 2U;
  const auto wait = WaitForMultipleObjects(
      handle_count, handles.data(), FALSE, remaining_timeout_ms(deadline));
  if (wait == WAIT_OBJECT_0) {
    if (GetOverlappedResult(pipe, &overlapped, &transferred, FALSE)) {
      error = ERROR_SUCCESS;
      return PipeIoStatus::Completed;
    }
    error = GetLastError();
    return error == ERROR_OPERATION_ABORTED ? PipeIoStatus::Cancelled
                                            : PipeIoStatus::Failed;
  }
  if (stop_event != nullptr && wait == WAIT_OBJECT_0 + 1) {
    CancelIoEx(pipe, &overlapped);
    GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
    error = ERROR_OPERATION_ABORTED;
    return PipeIoStatus::Cancelled;
  }
  if (wait == WAIT_TIMEOUT) {
    CancelIoEx(pipe, &overlapped);
    GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
    error = ERROR_TIMEOUT;
    return PipeIoStatus::TimedOut;
  }
  error = GetLastError();
  CancelIoEx(pipe, &overlapped);
  GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
  return PipeIoStatus::Failed;
}

PipeIoStatus transfer_pipe(HANDLE pipe,
                           void* buffer,
                           DWORD bytes,
                           bool write,
                           HANDLE io_event,
                           HANDLE stop_event,
                           PipeDeadline deadline,
                           DWORD& transferred,
                           DWORD& error) noexcept {
  if (remaining_timeout_ms(deadline) == 0) {
    error = ERROR_TIMEOUT;
    return PipeIoStatus::TimedOut;
  }
  ResetEvent(io_event);
  OVERLAPPED overlapped{};
  overlapped.hEvent = io_event;
  const BOOL started = write
                           ? WriteFile(pipe, buffer, bytes, &transferred, &overlapped)
                           : ReadFile(pipe, buffer, bytes, &transferred, &overlapped);
  if (started) {
    error = ERROR_SUCCESS;
    return PipeIoStatus::Completed;
  }
  error = GetLastError();
  if (error != ERROR_IO_PENDING) {
    return error == ERROR_OPERATION_ABORTED ? PipeIoStatus::Cancelled
                                            : PipeIoStatus::Failed;
  }
  return wait_for_pipe_io(
      pipe, overlapped, stop_event, deadline, transferred, error);
}

PipeIoStatus read_exact(HANDLE pipe,
                        std::span<std::byte> destination,
                        HANDLE io_event,
                        HANDLE stop_event,
                        PipeDeadline deadline,
                        DWORD& error) noexcept {
  std::size_t offset = 0;
  while (offset < destination.size()) {
    DWORD transferred = 0;
    const auto remaining = destination.size() - offset;
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
        remaining, std::numeric_limits<DWORD>::max()));
    const auto status = transfer_pipe(pipe, destination.data() + offset, chunk,
                                      false, io_event, stop_event, deadline,
                                      transferred, error);
    if (status != PipeIoStatus::Completed) {
      return status;
    }
    if (transferred == 0) {
      error = ERROR_BROKEN_PIPE;
      return PipeIoStatus::Failed;
    }
    offset += transferred;
  }
  error = ERROR_SUCCESS;
  return PipeIoStatus::Completed;
}

PipeIoStatus write_exact(HANDLE pipe,
                         std::span<const std::byte> source,
                         HANDLE io_event,
                         HANDLE stop_event,
                         PipeDeadline deadline,
                         DWORD& error) noexcept {
  std::size_t offset = 0;
  while (offset < source.size()) {
    DWORD transferred = 0;
    const auto remaining = source.size() - offset;
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
        remaining, std::numeric_limits<DWORD>::max()));
    const auto status = transfer_pipe(
        pipe, const_cast<std::byte*>(source.data() + offset), chunk, true,
        io_event, stop_event, deadline, transferred, error);
    if (status != PipeIoStatus::Completed) {
      return status;
    }
    if (transferred == 0) {
      error = ERROR_BROKEN_PIPE;
      return PipeIoStatus::Failed;
    }
    offset += transferred;
  }
  error = ERROR_SUCCESS;
  return PipeIoStatus::Completed;
}

bool valid_config(const NamedPipeControlConfig& config) noexcept {
  return !config.pipe_name.empty() && config.maximum_message_bytes != 0 &&
         config.request_timeout_ms != 0;
}

void close_pipe(HANDLE pipe) noexcept {
  if (pipe != INVALID_HANDLE_VALUE) {
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

WindowsNamedPipeControlServer::WindowsNamedPipeControlServer(
    NamedPipeControlConfig config,
    NamedPipeControlPeerHandler handler)
    : config_(std::move(config)), peer_handler_(std::move(handler)) {
  if (!peer_handler_) {
    throw std::invalid_argument(
        "Named pipe control server requires a peer handler");
  }
}

WindowsNamedPipeControlServer::~WindowsNamedPipeControlServer() {
  stop();
  if (stop_event_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(stop_event_));
  }
}

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
  reap_client_workers(true);
  try {
    std::lock_guard lock(clients_mutex_);
    client_workers_.reserve(kMaximumClientWorkers);
  } catch (const std::exception&) {
    return NamedPipeControlResult::failure(
        {"pipe_worker_reserve_failed", "Reserving named-pipe client workers failed.", 0});
  }

  if (stop_event_ == nullptr) {
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event_ == nullptr) {
      return NamedPipeControlResult::failure(win32_error(
          "pipe_stop_event_failed", "Creating the pipe stop event failed.",
          GetLastError()));
    }
  } else {
    ResetEvent(static_cast<HANDLE>(stop_event_));
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
  if (stop_event_ != nullptr) {
    SetEvent(static_cast<HANDLE>(stop_event_));
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  reap_client_workers(true);
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

void WindowsNamedPipeControlServer::reap_client_workers(bool join_all) noexcept {
  std::lock_guard lock(clients_mutex_);
  auto worker = client_workers_.begin();
  while (worker != client_workers_.end()) {
    if (!join_all && !worker->finished->load(std::memory_order_acquire)) {
      ++worker;
      continue;
    }
    if (worker->thread.joinable()) {
      worker->thread.join();
    }
    worker = client_workers_.erase(worker);
  }
}

void WindowsNamedPipeControlServer::serve_client(
    void* raw_pipe,
    std::uint32_t client_process_id,
    std::shared_ptr<std::atomic_bool> finished) noexcept {
  const auto pipe = static_cast<HANDLE>(raw_pipe);
  HANDLE io_event = nullptr;
  auto finish = [&] {
    if (io_event != nullptr) {
      CloseHandle(io_event);
    }
    close_pipe(pipe);
    finished->store(true, std::memory_order_release);
  };

  try {
    io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (io_event == nullptr) {
      protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      finish();
      return;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(config_.request_timeout_ms);
    std::array<std::byte, kFrameHeaderBytes> header{};
    DWORD native = ERROR_SUCCESS;
    const auto header_status =
        read_exact(pipe, header, io_event, static_cast<HANDLE>(stop_event_), deadline, native);
    if (header_status != PipeIoStatus::Completed) {
      if (header_status != PipeIoStatus::Cancelled && native != ERROR_BROKEN_PIPE &&
          native != ERROR_NO_DATA) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      }
      finish();
      return;
    }

    const auto request_length = decode_length(header);
    if (request_length > config_.maximum_message_bytes) {
      protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      finish();
      return;
    }

    std::vector<std::byte> request(request_length);
    const auto request_status =
        read_exact(pipe, request, io_event, static_cast<HANDLE>(stop_event_), deadline, native);
    if (request_status != PipeIoStatus::Completed) {
      if (request_status != PipeIoStatus::Cancelled) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      }
      finish();
      return;
    }

    NamedPipeControlResult response = NamedPipeControlResult::failure(
        {"pipe_handler_failed", "Named pipe control handler failed.", 0});
    try {
      if (peer_handler_) {
        response = peer_handler_({client_process_id}, request);
      } else {
        response = handler_(request);
      }
    } catch (const std::exception& error) {
      response = NamedPipeControlResult::failure(
          {"pipe_handler_exception", error.what(), 0});
    } catch (...) {
      response = NamedPipeControlResult::failure(
          {"pipe_handler_exception", "Named pipe handler threw an unknown exception.", 0});
    }
    if (!response.ok() || response.payload().size() > config_.maximum_message_bytes) {
      handler_errors_.fetch_add(1, std::memory_order_relaxed);
      finish();
      return;
    }

    const auto response_header =
        encode_length(static_cast<std::uint32_t>(response.payload().size()));
    const auto header_write_status = write_exact(
        pipe, response_header, io_event, static_cast<HANDLE>(stop_event_), deadline, native);
    const auto payload_write_status = header_write_status == PipeIoStatus::Completed
                                          ? write_exact(pipe, response.payload(), io_event,
                                                        static_cast<HANDLE>(stop_event_), deadline,
                                                        native)
                                          : header_write_status;
    if (payload_write_status != PipeIoStatus::Completed) {
      if (payload_write_status != PipeIoStatus::Cancelled) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      }
      finish();
      return;
    }

    completed_requests_.fetch_add(1, std::memory_order_relaxed);
  } catch (...) {
    handler_errors_.fetch_add(1, std::memory_order_relaxed);
  }
  finish();
}

void WindowsNamedPipeControlServer::run() noexcept {
  const auto path = full_pipe_name(config_.pipe_name);
  const auto stop_event = static_cast<HANDLE>(stop_event_);
  bool startup_published = false;

  auto security_result =
      platform::WindowsVirtualAsioSecurityAttributes::create_for_current_user();
  if (!security_result.ok()) {
    while (error_lock_.test_and_set(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    const auto& error = security_result.errors().front();
    last_errors_.push_back(win32_error(
        "pipe_security_create_failed",
        "Could not create named-pipe security attributes.",
        error.native_error));
    error_lock_.clear(std::memory_order_release);
    startup_succeeded_.store(false, std::memory_order_release);
    startup_complete_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    return;
  }
  auto security = security_result.take_attributes();

  while (!stop_requested_.load(std::memory_order_acquire)) {
    reap_client_workers(false);
    HANDLE pipe = CreateNamedPipeW(
        path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1,
        config_.maximum_message_bytes + kFrameHeaderBytes,
        config_.maximum_message_bytes + kFrameHeaderBytes, 0,
        static_cast<SECURITY_ATTRIBUTES*>(security->native_attributes()));
    if (pipe == INVALID_HANDLE_VALUE) {
      const auto native = GetLastError();
      while (error_lock_.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      last_errors_.push_back(win32_error(
          "pipe_create_failed", "CreateNamedPipeW failed.", native));
      error_lock_.clear(std::memory_order_release);
      if (!startup_published) {
        startup_succeeded_.store(false, std::memory_order_release);
        startup_complete_.store(true, std::memory_order_release);
      }
      break;
    }

    HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (io_event == nullptr) {
      const auto native = GetLastError();
      CloseHandle(pipe);
      while (error_lock_.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      last_errors_.push_back(win32_error(
          "pipe_io_event_failed", "Creating a pipe I/O event failed.", native));
      error_lock_.clear(std::memory_order_release);
      break;
    }

    if (!startup_published) {
      startup_succeeded_.store(true, std::memory_order_release);
      startup_complete_.store(true, std::memory_order_release);
      startup_published = true;
    }

    OVERLAPPED connect_overlapped{};
    connect_overlapped.hEvent = io_event;
    BOOL connected = ConnectNamedPipe(pipe, &connect_overlapped);
    if (!connected) {
      const auto connect_error = GetLastError();
      if (connect_error == ERROR_PIPE_CONNECTED) {
        connected = TRUE;
      } else if (connect_error == ERROR_IO_PENDING) {
        DWORD ignored = 0;
        DWORD native = ERROR_SUCCESS;
        connected = wait_for_pipe_io(pipe, connect_overlapped, stop_event,
                                     PipeDeadline::max(), ignored, native) ==
                    PipeIoStatus::Completed;
      }
    }
    if (!connected) {
      CloseHandle(io_event);
      CloseHandle(pipe);
      if (!stop_requested_.load(std::memory_order_acquire)) {
        protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      }
      continue;
    }
    accepted_connections_.fetch_add(1, std::memory_order_relaxed);
    if (stop_requested_.load(std::memory_order_acquire)) {
      CloseHandle(io_event);
      close_pipe(pipe);
      break;
    }

    ULONG client_process_id = 0;
    if (!GetNamedPipeClientProcessId(pipe, &client_process_id) ||
        client_process_id == 0) {
      protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      CloseHandle(io_event);
      close_pipe(pipe);
      continue;
    }
    CloseHandle(io_event);

    bool has_capacity = false;
    {
      std::lock_guard lock(clients_mutex_);
      has_capacity = client_workers_.size() < kMaximumClientWorkers;
    }
    if (!has_capacity) {
      protocol_errors_.fetch_add(1, std::memory_order_relaxed);
      close_pipe(pipe);
      continue;
    }

    try {
      auto finished = std::make_shared<std::atomic_bool>(false);
      std::thread worker([this, pipe, client_process_id, finished] {
        serve_client(pipe, static_cast<std::uint32_t>(client_process_id), finished);
      });
      std::lock_guard lock(clients_mutex_);
      client_workers_.push_back({std::move(worker), std::move(finished)});
    } catch (...) {
      handler_errors_.fetch_add(1, std::memory_order_relaxed);
      close_pipe(pipe);
    }
  }

  stop_requested_.store(true, std::memory_order_release);
  SetEvent(stop_event);
  reap_client_workers(true);
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
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  HANDLE pipe = INVALID_HANDLE_VALUE;
  DWORD connect_error = ERROR_SUCCESS;
  do {
    pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                       nullptr);
    if (pipe != INVALID_HANDLE_VALUE) {
      break;
    }
    connect_error = GetLastError();

    const bool transient = connect_error == ERROR_FILE_NOT_FOUND ||
                           connect_error == ERROR_PIPE_BUSY;
    if (!transient || std::chrono::steady_clock::now() >= deadline) {
      return NamedPipeControlResult::failure(win32_error(
          "pipe_connect_failed", "Could not connect to the named pipe before timeout.",
          connect_error));
    }
    Sleep(1);
  } while (true);

  HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (io_event == nullptr) {
    const auto native = GetLastError();
    CloseHandle(pipe);
    return NamedPipeControlResult::failure(win32_error(
        "pipe_io_event_failed", "Creating the client pipe I/O event failed.", native));
  }

  DWORD native = ERROR_SUCCESS;
  const auto request_header =
      encode_length(static_cast<std::uint32_t>(request.size()));
  const auto request_header_status =
      write_exact(pipe, request_header, io_event, nullptr, deadline, native);
  const auto request_status = request_header_status == PipeIoStatus::Completed
                                  ? write_exact(pipe, request, io_event, nullptr, deadline, native)
                                  : request_header_status;
  if (request_status != PipeIoStatus::Completed) {
    CloseHandle(io_event);
    CloseHandle(pipe);
    return NamedPipeControlResult::failure(win32_error(
        request_status == PipeIoStatus::TimedOut ? "pipe_write_timeout" : "pipe_write_failed",
        "Writing the named pipe request failed.", native));
  }

  std::array<std::byte, kFrameHeaderBytes> response_header{};
  const auto response_header_status =
      read_exact(pipe, response_header, io_event, nullptr, deadline, native);
  if (response_header_status != PipeIoStatus::Completed) {
    CloseHandle(io_event);
    CloseHandle(pipe);
    return NamedPipeControlResult::failure(win32_error(
        response_header_status == PipeIoStatus::TimedOut ? "pipe_read_timeout" : "pipe_read_failed",
        "Reading the named pipe response header failed.", native));
  }
  const auto response_length = decode_length(response_header);
  if (response_length > config.maximum_message_bytes) {
    CloseHandle(io_event);
    CloseHandle(pipe);
    return NamedPipeControlResult::failure(
        {"pipe_response_too_large", "Named pipe response exceeds the configured limit.", 0});
  }
  std::vector<std::byte> response(response_length);
  const auto response_status =
      read_exact(pipe, response, io_event, nullptr, deadline, native);
  if (response_status != PipeIoStatus::Completed) {
    CloseHandle(io_event);
    CloseHandle(pipe);
    return NamedPipeControlResult::failure(win32_error(
        response_status == PipeIoStatus::TimedOut ? "pipe_read_timeout" : "pipe_read_failed",
        "Reading the named pipe response failed.", native));
  }
  CloseHandle(io_event);
  CloseHandle(pipe);
  return NamedPipeControlResult::success(std::move(response));
}

}  // namespace sar::service
