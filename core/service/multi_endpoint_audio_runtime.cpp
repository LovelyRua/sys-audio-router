#include "core/service/multi_endpoint_audio_runtime.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace sar::service {

namespace {

void validate_member(const AudioRuntimeMember& member,
                     std::unordered_set<std::string>& endpoint_ids) {
  if (member.endpoint_id.empty()) {
    throw std::invalid_argument("Audio runtime member ID must not be empty");
  }
  if (!member.runtime) {
    throw std::invalid_argument("Audio runtime member must own a runtime");
  }
  if (!endpoint_ids.insert(member.endpoint_id).second) {
    throw std::invalid_argument("Audio runtime member IDs must be unique");
  }
}

void prefix_errors(std::vector<EngineAudioRuntimeError>& destination,
                   std::string_view endpoint_id,
                   const std::vector<EngineAudioRuntimeError>& source) {
  destination.reserve(destination.size() + source.size());
  for (const auto& error : source) {
    auto copy = error;
    copy.message = "Endpoint '" + std::string(endpoint_id) + "': " +
                   copy.message;
    destination.push_back(std::move(copy));
  }
}

int health_rank(EngineAudioRuntimeHealth health) noexcept {
  switch (health) {
    case EngineAudioRuntimeHealth::Stopped:
      return 0;
    case EngineAudioRuntimeHealth::Healthy:
      return 1;
    case EngineAudioRuntimeHealth::Degraded:
      return 2;
    case EngineAudioRuntimeHealth::Faulted:
      return 3;
  }
  return 3;
}

int state_rank(EngineAudioRecoveryState state) noexcept {
  switch (state) {
    case EngineAudioRecoveryState::Stopped:
      return 0;
    case EngineAudioRecoveryState::Running:
      return 1;
    case EngineAudioRecoveryState::Opening:
      return 2;
    case EngineAudioRecoveryState::Quiescing:
      return 3;
    case EngineAudioRecoveryState::Backoff:
      return 4;
    case EngineAudioRecoveryState::Faulted:
      return 5;
  }
  return 5;
}

void add_follower_diagnostics(diagnostics::EngineDiagnostics& aggregate,
                              const diagnostics::EngineDiagnostics& follower) {
  aggregate.xrun_count += follower.xrun_count;
  aggregate.capture_fifo_fill_frames += follower.capture_fifo_fill_frames;
  aggregate.render_fifo_fill_frames += follower.render_fifo_fill_frames;
  aggregate.capture_fifo_overflow_cycles +=
      follower.capture_fifo_overflow_cycles;
  aggregate.capture_fifo_overflow_frames +=
      follower.capture_fifo_overflow_frames;
  aggregate.render_fifo_overflow_cycles += follower.render_fifo_overflow_cycles;
  aggregate.render_fifo_overflow_frames += follower.render_fifo_overflow_frames;
  aggregate.render_fifo_underflow_cycles +=
      follower.render_fifo_underflow_cycles;
  aggregate.render_fifo_underflow_frames +=
      follower.render_fifo_underflow_frames;
  aggregate.sample_conversion_import_failures +=
      follower.sample_conversion_import_failures;
  aggregate.sample_conversion_export_failures +=
      follower.sample_conversion_export_failures;
  aggregate.virtual_asio_pushed_blocks += follower.virtual_asio_pushed_blocks;
  aggregate.virtual_asio_dropped_blocks += follower.virtual_asio_dropped_blocks;
  aggregate.virtual_asio_producer_underflows +=
      follower.virtual_asio_producer_underflows;
  aggregate.virtual_asio_producer_overflows +=
      follower.virtual_asio_producer_overflows;
  aggregate.virtual_asio_consumed_blocks +=
      follower.virtual_asio_consumed_blocks;
  aggregate.virtual_asio_mixed_blocks += follower.virtual_asio_mixed_blocks;
  aggregate.virtual_asio_silent_reads += follower.virtual_asio_silent_reads;
  aggregate.virtual_asio_clipped_samples +=
      follower.virtual_asio_clipped_samples;
  aggregate.virtual_asio_non_finite_samples +=
      follower.virtual_asio_non_finite_samples;
  aggregate.virtual_asio_maximum_queue_depth =
      std::max(aggregate.virtual_asio_maximum_queue_depth,
               follower.virtual_asio_maximum_queue_depth);
  aggregate.virtual_asio_active_producers +=
      follower.virtual_asio_active_producers;
  aggregate.virtual_asio_peak =
      std::max(aggregate.virtual_asio_peak, follower.virtual_asio_peak);
  aggregate.last_callback_seconds =
      std::max(aggregate.last_callback_seconds, follower.last_callback_seconds);
  aggregate.peak_callback_seconds =
      std::max(aggregate.peak_callback_seconds, follower.peak_callback_seconds);
}

void merge_recovery(EngineAudioRecoveryDiagnostics& aggregate,
                    std::string_view endpoint_id,
                    const EngineAudioRecoveryDiagnostics& member) {
  if (health_rank(member.runtime_health) >
      health_rank(aggregate.runtime_health)) {
    aggregate.runtime_health = member.runtime_health;
    aggregate.runtime_reason_code = member.runtime_reason_code.empty()
                                        ? std::string(endpoint_id)
                                        : std::string(endpoint_id) + ":" +
                                              member.runtime_reason_code;
  }
  if (state_rank(member.state) > state_rank(aggregate.state)) {
    aggregate.state = member.state;
  }
  aggregate.recovery_episode_count += member.recovery_episode_count;
  aggregate.successful_recovery_count += member.successful_recovery_count;
  aggregate.failed_recovery_count += member.failed_recovery_count;
  aggregate.last_recovery_duration_ms =
      std::max(aggregate.last_recovery_duration_ms,
               member.last_recovery_duration_ms);
  aggregate.maximum_recovery_duration_ms =
      std::max(aggregate.maximum_recovery_duration_ms,
               member.maximum_recovery_duration_ms);
  aggregate.endpoint_notification_reopen_count +=
      member.endpoint_notification_reopen_count;
  aggregate.endpoint_notification_reset_failure_count +=
      member.endpoint_notification_reset_failure_count;
  aggregate.endpoint_notification_reopen_pending =
      aggregate.endpoint_notification_reopen_pending ||
      member.endpoint_notification_reopen_pending;
  aggregate.wait_timeout_cycles += member.wait_timeout_cycles;
  aggregate.capture_discontinuity_cycles +=
      member.capture_discontinuity_cycles;
  aggregate.render_fifo_underflow_frames +=
      member.render_fifo_underflow_frames;
  aggregate.maximum_render_recovery_silence_frames =
      std::max(aggregate.maximum_render_recovery_silence_frames,
               member.maximum_render_recovery_silence_frames);
  aggregate.maximum_consecutive_capture_rate_clamped_frames =
      std::max(aggregate.maximum_consecutive_capture_rate_clamped_frames,
               member.maximum_consecutive_capture_rate_clamped_frames);
}

}  // namespace

MultiEndpointAudioRuntime::MultiEndpointAudioRuntime(
    AudioRuntimeMember master,
    std::vector<AudioRuntimeMember> followers)
    : master_(std::move(master)), followers_(std::move(followers)) {
  std::unordered_set<std::string> endpoint_ids;
  validate_member(master_, endpoint_ids);
  for (const auto& follower : followers_) {
    validate_member(follower, endpoint_ids);
  }
}

MultiEndpointAudioRuntime::~MultiEndpointAudioRuntime() {
  stop();
}

EngineAudioRuntimeResult MultiEndpointAudioRuntime::start(
    std::uint32_t timeout_ms) {
  std::lock_guard lock(lifecycle_mutex_);
  if (running_) {
    return EngineAudioRuntimeResult::failure({
        {"multi_endpoint_runtime_already_running",
         "Multi-endpoint audio runtime is already running."},
    });
  }

  std::size_t started_followers = 0;
  for (auto& follower : followers_) {
    const auto result = follower.runtime->start(timeout_ms);
    if (!result.ok()) {
      std::vector<EngineAudioRuntimeError> errors;
      prefix_errors(errors, follower.endpoint_id, result.errors());
      follower.runtime->stop();
      while (started_followers != 0) {
        --started_followers;
        followers_[started_followers].runtime->stop();
      }
      return EngineAudioRuntimeResult::failure(std::move(errors));
    }
    ++started_followers;
  }

  const auto master_result = master_.runtime->start(timeout_ms);
  if (!master_result.ok()) {
    std::vector<EngineAudioRuntimeError> errors;
    prefix_errors(errors, master_.endpoint_id, master_result.errors());
    master_.runtime->stop();
    for (auto follower = followers_.rbegin(); follower != followers_.rend();
         ++follower) {
      follower->runtime->stop();
    }
    return EngineAudioRuntimeResult::failure(std::move(errors));
  }

  running_ = true;
  return EngineAudioRuntimeResult::success();
}

void MultiEndpointAudioRuntime::stop() noexcept {
  std::lock_guard lock(lifecycle_mutex_);
  stop_locked();
}

void MultiEndpointAudioRuntime::stop_locked() noexcept {
  if (!running_) {
    return;
  }
  master_.runtime->stop();
  for (auto follower = followers_.rbegin(); follower != followers_.rend();
       ++follower) {
    follower->runtime->stop();
  }
  running_ = false;
}

bool MultiEndpointAudioRuntime::running() const noexcept {
  std::lock_guard lock(lifecycle_mutex_);
  if (!running_ || !master_.runtime->running()) {
    return false;
  }
  return std::ranges::all_of(followers_, [](const auto& follower) {
    return follower.runtime->running();
  });
}

std::uint64_t MultiEndpointAudioRuntime::graph_version() const noexcept {
  return master_.runtime->graph_version();
}

bool MultiEndpointAudioRuntime::apply_realtime_graph_parameters(
    const graph::Graph& graph) noexcept {
  return master_.runtime->apply_realtime_graph_parameters(graph);
}

diagnostics::EngineDiagnostics MultiEndpointAudioRuntime::diagnostics() const {
  auto aggregate = master_.runtime->diagnostics();
  for (const auto& follower : followers_) {
    add_follower_diagnostics(aggregate, follower.runtime->diagnostics());
  }
  aggregate.graph_version = graph_version();
  return aggregate;
}

std::optional<EngineAudioRecoveryDiagnostics>
MultiEndpointAudioRuntime::recovery_diagnostics() const {
  std::optional<EngineAudioRecoveryDiagnostics> aggregate;
  const auto add = [&aggregate](std::string_view endpoint_id,
                                const EngineAudioRuntime& runtime) {
    const auto member = runtime.recovery_diagnostics();
    if (!member) {
      return;
    }
    if (!aggregate) {
      aggregate = EngineAudioRecoveryDiagnostics{};
    }
    merge_recovery(*aggregate, endpoint_id, *member);
  };
  add(master_.endpoint_id, *master_.runtime);
  for (const auto& follower : followers_) {
    add(follower.endpoint_id, *follower.runtime);
  }
  return aggregate;
}

std::size_t MultiEndpointAudioRuntime::endpoint_count() const noexcept {
  return followers_.size() + 1;
}

std::string_view MultiEndpointAudioRuntime::master_endpoint_id() const noexcept {
  return master_.endpoint_id;
}

}  // namespace sar::service
