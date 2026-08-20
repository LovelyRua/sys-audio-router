#include "core/service/windows_physical_asio_engine_runtime.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "third_party/asio_sdk_2.3.4/common/asio.h"

namespace {

class UnusedActivator final
    : public sar::platform::WindowsAsioDriverActivator {
 public:
  std::unique_ptr<sar::platform::WindowsAsioActivatedDriver> activate(
      const std::string&) noexcept override {
    assert(false);
    return {};
  }
};

class UnusedNegotiator final
    : public sar::platform::WindowsAsioDriverNegotiator {
 public:
  sar::platform::WindowsAsioNegotiationResult negotiate(
      sar::platform::WindowsAsioActivatedDriver&,
      const sar::platform::WindowsAsioControlOpenRequest&) noexcept override {
    assert(false);
    return {};
  }
};

sar::control::AudioRuntimeConfiguration configuration() {
  sar::control::AudioRuntimeConfiguration result;
  result.mode = sar::control::AudioRuntimeMode::PhysicalAsio;
  result.physical_asio_driver_clsid = "{driver}";
  result.physical_asio_sample_rate = 48000;
  result.physical_asio_block_frames = 128;
  result.physical_asio_input_channels = {0, 1};
  result.physical_asio_output_channels = {0, 1};
  return result;
}

sar::platform::WindowsAsioDriverProbe probe() {
  sar::platform::WindowsAsioDriverProbe result;
  result.clsid = "{driver}";
  result.input_channels = 2;
  result.output_channels = 2;
  return result;
}

bool has_error(const sar::service::EngineAudioRuntimeBuildResult& result,
               const std::string& code) {
  return !result.ok() && !result.errors().empty() &&
         result.errors().front().code == code;
}

struct ThreadRecord {
  std::mutex mutex;
  std::vector<std::thread::id> calls;

  void add() {
    std::lock_guard lock(mutex);
    calls.push_back(std::this_thread::get_id());
  }
};

class RecordingLifecycle final
    : public sar::platform::WindowsAsioDriverLifecycle {
 public:
  explicit RecordingLifecycle(std::shared_ptr<ThreadRecord> record)
      : record_(std::move(record)) {}
  long create_buffers(ASIOBufferInfo* infos, long count, long frames,
                      ASIOCallbacks*) noexcept override {
    record_->add();
    try {
      buffers_.resize(static_cast<std::size_t>(count) * 2U);
      for (long index = 0; index < count; ++index) {
        for (std::size_t half = 0; half < 2; ++half) {
          auto& buffer = buffers_[static_cast<std::size_t>(index) * 2U + half];
          buffer.assign(static_cast<std::size_t>(frames), 0.0F);
          infos[index].buffers[half] = buffer.data();
        }
      }
    } catch (...) {
      return ASE_NoMemory;
    }
    return ASE_OK;
  }
  long start() noexcept override {
    record_->add();
    return ASE_OK;
  }
  long stop() noexcept override {
    record_->add();
    return ASE_OK;
  }
  long dispose_buffers() noexcept override {
    record_->add();
    return ASE_OK;
  }
  void release() noexcept override { record_->add(); }

 private:
  std::shared_ptr<ThreadRecord> record_;
  std::vector<std::vector<float>> buffers_;
};

class RecordingDriver final
    : public sar::platform::WindowsAsioActivatedDriver {
 public:
  explicit RecordingDriver(std::shared_ptr<ThreadRecord> record)
      : record_(std::move(record)) {}
  ~RecordingDriver() override { record_->add(); }
  bool initialize(void*) noexcept override {
    record_->add();
    return true;
  }
  bool can_sample_rate(double) noexcept override { return true; }
  bool set_sample_rate(double) noexcept override { return true; }
  bool channels(long&, long&) noexcept override { return false; }
  bool buffer_sizes(long&, long&, long&, long&) noexcept override {
    return false;
  }
  bool channel_info(long, bool, long&, std::string&) noexcept override {
    return false;
  }
  std::unique_ptr<sar::platform::WindowsAsioDriverLifecycle>
  take_lifecycle() noexcept override {
    record_->add();
    return std::make_unique<RecordingLifecycle>(record_);
  }

 private:
  std::shared_ptr<ThreadRecord> record_;
};

class RecordingActivator final
    : public sar::platform::WindowsAsioDriverActivator {
 public:
  explicit RecordingActivator(std::shared_ptr<ThreadRecord> record)
      : record_(std::move(record)) {}
  std::unique_ptr<sar::platform::WindowsAsioActivatedDriver> activate(
      const std::string&) noexcept override {
    record_->add();
    return std::make_unique<RecordingDriver>(record_);
  }

 private:
  std::shared_ptr<ThreadRecord> record_;
};

class RecordingNegotiator final
    : public sar::platform::WindowsAsioDriverNegotiator {
 public:
  explicit RecordingNegotiator(std::shared_ptr<ThreadRecord> record)
      : record_(std::move(record)) {}
  sar::platform::WindowsAsioNegotiationResult negotiate(
      sar::platform::WindowsAsioActivatedDriver& driver,
      const sar::platform::WindowsAsioControlOpenRequest&) noexcept override {
    record_->add();
    assert(driver.initialize(nullptr));
    sar::platform::WindowsAsioNegotiationResult result;
    result.config.sample_rate = 48000.0;
    result.config.frames_per_block = 128;
    for (std::uint32_t index = 0; index < 2; ++index) {
      result.config.inputs.push_back(
          {index, true, sar::platform::WindowsAsioSampleEncoding::Float32Lsb,
           "Input"});
      result.config.outputs.push_back(
          {index, false, sar::platform::WindowsAsioSampleEncoding::Float32Lsb,
           "Output"});
    }
    return result;
  }

 private:
  std::shared_ptr<ThreadRecord> record_;
};

void control_lifecycle_stays_on_one_thread() {
  auto record = std::make_shared<ThreadRecord>();
  RecordingActivator activator(record);
  RecordingNegotiator negotiator(record);
  auto automatic_channels = configuration();
  automatic_channels.physical_asio_input_channels.clear();
  automatic_channels.physical_asio_output_channels.clear();
  auto opened = sar::service::open_windows_physical_asio_engine_runtime(
      automatic_channels,
      std::make_shared<sar::graph::Graph>(31, 2, 128, 48000),
      [record](const std::string&) {
        record->add();
        return sar::platform::WindowsAsioDriverProbeResult::success(probe());
      },
      activator, negotiator);
  assert(opened.ok());
  auto runtime = opened.take_runtime();

  std::thread start_thread([&] { assert(runtime->start(10).ok()); });
  start_thread.join();
  std::thread stop_thread([&] { runtime->stop(); });
  stop_thread.join();
  std::thread destroy_thread(
      [owned = std::move(runtime)]() mutable { owned.reset(); });
  destroy_thread.join();

  std::lock_guard lock(record->mutex);
  assert(record->calls.size() >= 8);
  const auto control_thread = record->calls.front();
  assert(control_thread != std::this_thread::get_id());
  for (const auto thread : record->calls) assert(thread == control_thread);
}

}  // namespace

int main() {
  UnusedActivator activator;
  UnusedNegotiator negotiator;

  auto failed_probe = sar::service::open_windows_physical_asio_engine_runtime(
      configuration(), std::make_shared<sar::graph::Graph>(1, 2, 128, 48000),
      [](const std::string&) {
        return sar::platform::WindowsAsioDriverProbeResult::failure(
            {"probe_failed", "expected"});
      },
      activator, negotiator);
  assert(has_error(failed_probe, "physical_asio_driver_probe_failed"));

  auto sparse = configuration();
  sparse.physical_asio_input_channels = {1};
  auto sparse_result =
      sar::service::open_windows_physical_asio_engine_runtime(
          sparse, std::make_shared<sar::graph::Graph>(1, 2, 128, 48000),
          [](const std::string&) {
            return sar::platform::WindowsAsioDriverProbeResult::success(
                probe());
          },
          activator, negotiator);
  assert(has_error(sparse_result,
                   "physical_asio_channel_subset_not_implemented"));

  auto asymmetric = probe();
  asymmetric.input_channels = 8;
  asymmetric.output_channels = 2;
  auto direct = sar::service::build_windows_physical_asio_direct_graph(
      configuration(), asymmetric, 19);
  assert(direct);
  assert(direct->version() == 19);
  assert(direct->channels() == 8);
  assert(direct->frames() == 128);
  assert(direct->sample_rate() == 48000);
  control_lifecycle_stays_on_one_thread();
}
