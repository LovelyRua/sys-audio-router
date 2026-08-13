#include "core/service/multi_endpoint_audio_runtime.h"
#include "core/service/windows_wasapi_matrix_runtime.h"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeRuntime final : public sar::service::EngineAudioRuntime {
 public:
  FakeRuntime(std::string name,
              std::vector<std::string>& events,
              bool fail_start = false)
      : name_(std::move(name)), events_(events), fail_start_(fail_start) {}

  sar::service::EngineAudioRuntimeResult start(std::uint32_t) override {
    events_.push_back("start:" + name_);
    if (fail_start_) {
      return sar::service::EngineAudioRuntimeResult::failure(
          {{"injected_start_failure", "Injected start failure."}});
    }
    running_ = true;
    return sar::service::EngineAudioRuntimeResult::success();
  }

  void stop() noexcept override {
    events_.push_back("stop:" + name_);
    running_ = false;
  }

  bool running() const noexcept override { return running_; }
  std::uint64_t graph_version() const noexcept override { return version_; }
  bool apply_realtime_graph_parameters(
      const sar::graph::Graph& graph) noexcept override {
    ++parameter_calls;
    version_ = graph.version();
    return true;
  }
  sar::diagnostics::EngineDiagnostics diagnostics() const override {
    return diagnostics_;
  }
  std::optional<sar::service::EngineAudioRecoveryDiagnostics>
  recovery_diagnostics() const override {
    return recovery_;
  }

  bool running_ = false;
  std::uint64_t version_ = 7;
  std::uint32_t parameter_calls = 0;
  sar::diagnostics::EngineDiagnostics diagnostics_;
  std::optional<sar::service::EngineAudioRecoveryDiagnostics> recovery_;

 private:
  std::string name_;
  std::vector<std::string>& events_;
  bool fail_start_ = false;
};

sar::service::AudioRuntimeMember member(
    std::string id,
    std::unique_ptr<sar::service::EngineAudioRuntime> runtime) {
  return {std::move(id), std::move(runtime)};
}

}  // namespace

int main() {
  std::vector<std::string> events;
  auto master = std::make_unique<FakeRuntime>("master", events);
  auto* master_observer = master.get();
  auto follower_a = std::make_unique<FakeRuntime>("follower-a", events);
  auto* follower_a_observer = follower_a.get();
  auto follower_b = std::make_unique<FakeRuntime>("follower-b", events);
  auto* follower_b_observer = follower_b.get();
  master_observer->diagnostics_.processed_blocks = 100;
  master_observer->diagnostics_.xrun_count = 1;
  master_observer->diagnostics_.peak_callback_seconds = 0.001;
  follower_a_observer->diagnostics_.processed_blocks = 500;
  follower_a_observer->diagnostics_.xrun_count = 2;
  follower_a_observer->diagnostics_.render_fifo_underflow_frames = 128;
  follower_a_observer->diagnostics_.virtual_asio_silent_reads = 4;
  follower_a_observer->diagnostics_.virtual_asio_maximum_queue_depth = 7;
  follower_b_observer->diagnostics_.xrun_count = 3;
  follower_b_observer->diagnostics_.peak_callback_seconds = 0.002;
  follower_b_observer->recovery_ =
      sar::service::EngineAudioRecoveryDiagnostics{
          .state = sar::service::EngineAudioRecoveryState::Backoff,
          .runtime_health = sar::service::EngineAudioRuntimeHealth::Degraded,
          .runtime_reason_code = "render_underflow",
          .recovery_episode_count = 2,
      };

  std::vector<sar::service::AudioRuntimeMember> followers;
  followers.push_back(member("render-a", std::move(follower_a)));
  followers.push_back(member("render-b", std::move(follower_b)));
  sar::service::MultiEndpointAudioRuntime runtime(
      member("render-main", std::move(master)), std::move(followers));
  assert(runtime.endpoint_count() == 3);
  assert(runtime.master_endpoint_id() == "render-main");
  assert(runtime.start(10).ok());
  assert(runtime.running());
  assert((events == std::vector<std::string>{"start:follower-a",
                                             "start:follower-b",
                                             "start:master"}));

  const auto diagnostics = runtime.diagnostics();
  assert(diagnostics.processed_blocks == 100);
  assert(diagnostics.xrun_count == 6);
  assert(diagnostics.render_fifo_underflow_frames == 128);
  assert(diagnostics.virtual_asio_silent_reads == 4);
  assert(diagnostics.virtual_asio_maximum_queue_depth == 7);
  assert(diagnostics.peak_callback_seconds == 0.002);
  const auto recovery = runtime.recovery_diagnostics();
  assert(recovery.has_value());
  assert(recovery->runtime_health ==
         sar::service::EngineAudioRuntimeHealth::Degraded);
  assert(recovery->runtime_reason_code == "render-b:render_underflow");

  auto endpoint_diagnostics = runtime.endpoint_diagnostics();
  assert(endpoint_diagnostics.size() == 3);
  assert(endpoint_diagnostics[0].endpoint_id == "render-main");
  assert(endpoint_diagnostics[0].role ==
         sar::service::EngineAudioEndpointRole::Master);
  assert(endpoint_diagnostics[0].diagnostics.processed_blocks == 100);
  assert(endpoint_diagnostics[0].diagnostics.xrun_count == 1);
  assert(!endpoint_diagnostics[0].recovery.has_value());
  assert(!endpoint_diagnostics[0].queue_fill_frames.has_value());
  assert(!endpoint_diagnostics[0].correction_ppm.has_value());

  assert(endpoint_diagnostics[1].endpoint_id == "render-a");
  assert(endpoint_diagnostics[1].role ==
         sar::service::EngineAudioEndpointRole::Follower);
  assert(endpoint_diagnostics[1].diagnostics.processed_blocks == 500);
  assert(endpoint_diagnostics[1].diagnostics.xrun_count == 2);
  assert(endpoint_diagnostics[1].diagnostics.render_fifo_underflow_frames ==
         128);
  assert(!endpoint_diagnostics[1].recovery.has_value());

  assert(endpoint_diagnostics[2].endpoint_id == "render-b");
  assert(endpoint_diagnostics[2].role ==
         sar::service::EngineAudioEndpointRole::Follower);
  assert(endpoint_diagnostics[2].diagnostics.xrun_count == 3);
  assert(endpoint_diagnostics[2].recovery.has_value());
  assert(endpoint_diagnostics[2].recovery->state ==
         sar::service::EngineAudioRecoveryState::Backoff);
  assert(endpoint_diagnostics[2].recovery->runtime_reason_code ==
         "render_underflow");

  sar::diagnostics::EngineDiagnostics resource_diagnostics;
  resource_diagnostics.xrun_count = 4;
  resource_diagnostics.virtual_asio_pushed_blocks = 12;
  resource_diagnostics.virtual_asio_dropped_blocks = 4;
  resource_diagnostics.virtual_asio_producer_overflows = 2;
  resource_diagnostics.virtual_asio_maximum_queue_depth = 9;
  sar::service::merge_windows_wasapi_matrix_endpoint_diagnostics(
      endpoint_diagnostics,
      {{.endpoint_id = "render-a",
        .diagnostics = resource_diagnostics,
        .queue_fill_frames = 384,
        .correction_ppm = -17.25},
       {.endpoint_id = "missing-endpoint",
        .queue_fill_frames = 999,
        .correction_ppm = 999.0}});
  assert(endpoint_diagnostics[1].diagnostics.xrun_count == 6);
  assert(endpoint_diagnostics[1].diagnostics.virtual_asio_pushed_blocks ==
         12);
  assert(endpoint_diagnostics[1].diagnostics.virtual_asio_dropped_blocks ==
         4);
  assert(
      endpoint_diagnostics[1].diagnostics.virtual_asio_producer_overflows ==
      2);
  assert(endpoint_diagnostics[1]
             .diagnostics.virtual_asio_maximum_queue_depth == 9);
  assert(endpoint_diagnostics[1].queue_fill_frames == 384);
  assert(endpoint_diagnostics[1].correction_ppm == -17.25);
  assert(!endpoint_diagnostics[0].queue_fill_frames.has_value());
  assert(!endpoint_diagnostics[2].queue_fill_frames.has_value());

  sar::graph::Graph next_graph(8, 2, 128, 48000);
  assert(runtime.apply_realtime_graph_parameters(next_graph));
  assert(master_observer->parameter_calls == 1);
  assert(follower_a_observer->parameter_calls == 0);

  runtime.stop();
  assert(!runtime.running());
  assert(events[3] == "stop:master");
  assert(events[4] == "stop:follower-b");
  assert(events[5] == "stop:follower-a");

  events.clear();
  auto rollback_a = std::make_unique<FakeRuntime>("rollback-a", events);
  auto* rollback_a_observer = rollback_a.get();
  std::vector<sar::service::AudioRuntimeMember> rollback_followers;
  rollback_followers.push_back(
      member("rollback-a", std::move(rollback_a)));
  rollback_followers.push_back(member(
      "failing", std::make_unique<FakeRuntime>("failing", events, true)));
  sar::service::MultiEndpointAudioRuntime rollback(
      member("master", std::make_unique<FakeRuntime>("master", events)),
      std::move(rollback_followers));
  const auto failed = rollback.start(10);
  assert(!failed.ok());
  assert(!rollback_a_observer->running());
  assert((events == std::vector<std::string>{"start:rollback-a",
                                             "start:failing",
                                             "stop:failing",
                                             "stop:rollback-a"}));
  assert(failed.errors()[0].message.find("failing") != std::string::npos);
  return 0;
}
