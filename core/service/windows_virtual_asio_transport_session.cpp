#include "core/service/windows_virtual_asio_transport_session.h"

#include "core/platform/windows_realtime_thread.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <new>
#include <system_error>
#include <utility>

namespace sar::service {
namespace {

class HandleOwner {
 public:
  explicit HandleOwner(HANDLE handle) noexcept : handle_(handle) {}
  HandleOwner(const HandleOwner&) = delete;
  HandleOwner& operator=(const HandleOwner&) = delete;
  ~HandleOwner() {
    if (handle_ != nullptr) {
      CloseHandle(handle_);
    }
  }

  [[nodiscard]] HANDLE release() noexcept {
    return std::exchange(handle_, nullptr);
  }

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }

 private:
  HANDLE handle_ = nullptr;
};

WindowsVirtualAsioTransportSessionCreateResult failure(
    std::string code,
    std::string message,
    std::uint32_t native_error = 0) {
  return WindowsVirtualAsioTransportSessionCreateResult::failure({
      {std::move(code), std::move(message), native_error},
  });
}

}  // namespace

WindowsVirtualAsioTransportSessionCreateResult
WindowsVirtualAsioTransportSessionCreateResult::success(
    std::unique_ptr<WindowsVirtualAsioTransportSession> session) {
  return {std::move(session), {}};
}

WindowsVirtualAsioTransportSessionCreateResult
WindowsVirtualAsioTransportSessionCreateResult::failure(
    std::vector<WindowsVirtualAsioTransportError> errors) {
  return {nullptr, std::move(errors)};
}

bool WindowsVirtualAsioTransportSessionCreateResult::ok() const noexcept {
  return session_ != nullptr && errors_.empty();
}

WindowsVirtualAsioTransportSession&
WindowsVirtualAsioTransportSessionCreateResult::session() noexcept {
  return *session_;
}

std::unique_ptr<WindowsVirtualAsioTransportSession>
WindowsVirtualAsioTransportSessionCreateResult::take_session() noexcept {
  return std::move(session_);
}

const std::vector<WindowsVirtualAsioTransportError>&
WindowsVirtualAsioTransportSessionCreateResult::errors() const noexcept {
  return errors_;
}

WindowsVirtualAsioTransportSessionCreateResult::
    WindowsVirtualAsioTransportSessionCreateResult(
        std::unique_ptr<WindowsVirtualAsioTransportSession> session,
        std::vector<WindowsVirtualAsioTransportError> errors)
    : session_(std::move(session)), errors_(std::move(errors)) {}

WindowsVirtualAsioTransportSession::~WindowsVirtualAsioTransportSession() {
  stop();
  if (client_process_handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(client_process_handle_));
  }
}

WindowsVirtualAsioTransportSessionCreateResult
WindowsVirtualAsioTransportSession::create(
    platform::WindowsVirtualAsioObjectNames names,
    const platform::VirtualAsioSharedMemoryConfig& config,
    const platform::VirtualAsioSharedMemoryIdentity& identity,
    std::unique_ptr<graph::Graph> graph,
    std::uint32_t wait_timeout_ms) {
  if (graph == nullptr) {
    return failure("virtual_asio_session_graph_missing",
                   "Virtual ASIO transport requires an owned graph.");
  }
  if (wait_timeout_ms == 0) {
    return failure("virtual_asio_session_timeout_invalid",
                   "Virtual ASIO transport wait timeout must be non-zero.");
  }
  if (config.format.input_channels == 0 ||
      config.format.output_channels == 0 ||
      graph->sample_rate() != config.format.sample_rate ||
      graph->frames() != config.format.frames_per_block ||
      graph->channels() != config.format.input_channels ||
      graph->channels() != config.format.output_channels) {
    return failure("virtual_asio_session_graph_format_mismatch",
                   "Virtual ASIO transport requires a strict duplex graph format match.");
  }

  try {
    HandleOwner client_process(OpenProcess(
        SYNCHRONIZE, FALSE, identity.client_process_id));
    if (client_process.get() == nullptr) {
      return failure("virtual_asio_client_process_open_failed",
                     "Could not monitor the Virtual ASIO client process.",
                     GetLastError());
    }

    auto mapping_result = platform::WindowsVirtualAsioSharedMemory::create(
        names.mapping, config, identity);
    if (!mapping_result.ok()) {
      std::vector<WindowsVirtualAsioTransportError> errors;
      for (const auto& error : mapping_result.errors()) {
        errors.push_back({error.code, error.message, error.native_error});
      }
      return WindowsVirtualAsioTransportSessionCreateResult::failure(
          std::move(errors));
    }
    auto mapping = mapping_result.take_mapping();

    auto events_result =
        platform::WindowsVirtualAsioEvents::create(std::move(names));
    if (!events_result.ok()) {
      std::vector<WindowsVirtualAsioTransportError> errors;
      for (const auto& error : events_result.errors()) {
        errors.push_back({error.code, error.message, error.native_error});
      }
      return WindowsVirtualAsioTransportSessionCreateResult::failure(
          std::move(errors));
    }
    auto events = events_result.take_events();

    auto input_result = platform::WindowsVirtualAsioSharedQueue::bind(
        *mapping, platform::VirtualAsioSharedQueueDirection::Input);
    if (!input_result.ok()) {
      return failure(input_result.error_code(),
                     "Could not bind the Virtual ASIO input queue.");
    }
    auto output_result = platform::WindowsVirtualAsioSharedQueue::bind(
        *mapping, platform::VirtualAsioSharedQueueDirection::Output);
    if (!output_result.ok()) {
      return failure(output_result.error_code(),
                     "Could not bind the Virtual ASIO output queue.");
    }

    return WindowsVirtualAsioTransportSessionCreateResult::success(
        std::unique_ptr<WindowsVirtualAsioTransportSession>(
            new WindowsVirtualAsioTransportSession(
                std::move(mapping), std::move(events),
                input_result.take_queue(), output_result.take_queue(),
                std::move(graph), client_process.release(), wait_timeout_ms)));
  } catch (const std::bad_alloc&) {
    return failure("virtual_asio_session_allocation_failed",
                   "Could not allocate Virtual ASIO transport state.");
  }
}

bool WindowsVirtualAsioTransportSession::start() noexcept {
  if (running_.load(std::memory_order_acquire) ||
      started_once_.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }
  if (!events_->reset_shutdown()) {
    return false;
  }

  running_.store(true, std::memory_order_release);
  mapping_->set_state(platform::VirtualAsioSharedMemoryState::Ready);
  try {
    thread_ = std::thread([this] { run(); });
  } catch (const std::system_error&) {
    mapping_->set_state(platform::VirtualAsioSharedMemoryState::Faulted);
    running_.store(false, std::memory_order_release);
    return false;
  }
  return true;
}

void WindowsVirtualAsioTransportSession::stop() noexcept {
  if (!started_once_.load(std::memory_order_acquire)) {
    return;
  }
  mapping_->set_state(platform::VirtualAsioSharedMemoryState::Stopping);
  static_cast<void>(events_->signal_shutdown());
  if (thread_.joinable()) {
    thread_.join();
  }
  running_.store(false, std::memory_order_release);
}

bool WindowsVirtualAsioTransportSession::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

const platform::WindowsVirtualAsioObjectNames&
WindowsVirtualAsioTransportSession::names() const noexcept {
  return events_->names();
}

platform::VirtualAsioSharedMemoryState
WindowsVirtualAsioTransportSession::shared_state() const noexcept {
  return mapping_->state();
}

WindowsVirtualAsioTransportStats
WindowsVirtualAsioTransportSession::stats() const noexcept {
  return {
      running(),
      processed_blocks_.load(std::memory_order_relaxed),
      dropped_output_blocks_.load(std::memory_order_relaxed),
      input_queue_errors_.load(std::memory_order_relaxed),
      output_queue_errors_.load(std::memory_order_relaxed),
      wait_timeouts_.load(std::memory_order_relaxed),
      wait_failures_.load(std::memory_order_relaxed),
      output_signal_failures_.load(std::memory_order_relaxed),
      realtime_thread_failures_.load(std::memory_order_relaxed),
      client_process_exits_.load(std::memory_order_relaxed),
      last_sequence_.load(std::memory_order_relaxed),
  };
}

WindowsVirtualAsioTransportSession::WindowsVirtualAsioTransportSession(
    std::unique_ptr<platform::WindowsVirtualAsioSharedMemory> mapping,
    std::unique_ptr<platform::WindowsVirtualAsioEvents> events,
    platform::WindowsVirtualAsioSharedQueue input_queue,
    platform::WindowsVirtualAsioSharedQueue output_queue,
    std::unique_ptr<graph::Graph> graph,
    void* client_process_handle,
    std::uint32_t wait_timeout_ms)
    : mapping_(std::move(mapping)),
      events_(std::move(events)),
      input_queue_(std::move(input_queue)),
      output_queue_(std::move(output_queue)),
      graph_(std::move(graph)),
      input_buffer_(graph_->channels(), graph_->frames()),
      output_buffer_(graph_->channels(), graph_->frames()),
      client_process_handle_(client_process_handle),
      wait_timeout_ms_(wait_timeout_ms) {}

void WindowsVirtualAsioTransportSession::run() noexcept {
  platform::WindowsRealtimeThreadScope realtime_scope;
  const auto realtime_result =
      platform::WindowsRealtimeThreadScope::enter_current_thread(realtime_scope);
  if (!realtime_result.ok()) {
    realtime_thread_failures_.fetch_add(1, std::memory_order_relaxed);
  }

  while (running_.load(std::memory_order_acquire)) {
    if (client_process_exited()) {
      client_process_exits_.fetch_add(1, std::memory_order_relaxed);
      mapping_->set_state(platform::VirtualAsioSharedMemoryState::Stopping);
      break;
    }
    const auto wait = events_->wait_input_or_shutdown(wait_timeout_ms_);
    if (wait.status == platform::WindowsVirtualAsioEventWaitStatus::Shutdown) {
      break;
    }
    if (wait.status == platform::WindowsVirtualAsioEventWaitStatus::TimedOut) {
      wait_timeouts_.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    if (wait.status == platform::WindowsVirtualAsioEventWaitStatus::Failed) {
      wait_failures_.fetch_add(1, std::memory_order_relaxed);
      break;
    }
    process_available_input();
  }
  running_.store(false, std::memory_order_release);
}

bool WindowsVirtualAsioTransportSession::client_process_exited() noexcept {
  if (client_process_handle_ == nullptr) {
    return false;
  }
  return WaitForSingleObject(static_cast<HANDLE>(client_process_handle_), 0) ==
         WAIT_OBJECT_0;
}

void WindowsVirtualAsioTransportSession::process_available_input() noexcept {
  for (;;) {
    platform::VirtualAsioSharedBlockMetadata metadata;
    const auto input_status = input_queue_.pop(input_buffer_, metadata);
    if (input_status == platform::VirtualAsioSharedQueueStatus::Empty ||
        input_status == platform::VirtualAsioSharedQueueStatus::NotReady) {
      return;
    }
    if (input_status != platform::VirtualAsioSharedQueueStatus::Completed) {
      input_queue_errors_.fetch_add(1, std::memory_order_relaxed);
      if (input_status ==
          platform::VirtualAsioSharedQueueStatus::CorruptControl) {
        return;
      }
      continue;
    }

    graph_->process(input_buffer_, output_buffer_, graph_diagnostics_);
    processed_blocks_.fetch_add(1, std::memory_order_relaxed);
    last_sequence_.store(metadata.sequence, std::memory_order_relaxed);

    const auto output_status = output_queue_.push(output_buffer_, metadata);
    if (output_status == platform::VirtualAsioSharedQueueStatus::Full) {
      dropped_output_blocks_.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    if (output_status != platform::VirtualAsioSharedQueueStatus::Completed) {
      output_queue_errors_.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    if (!events_->signal_output()) {
      output_signal_failures_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

}  // namespace sar::service
