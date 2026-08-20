#include "core/service/windows_physical_asio_engine_runtime.h"

#include "core/service/windows_physical_asio_runtime.h"

#include "core/graph/node.h"

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace sar::service {
namespace {

EngineAudioRuntimeBuildResult failure(std::string code, std::string message) {
  return EngineAudioRuntimeBuildResult::failure(
      {{std::move(code), std::move(message)}});
}

bool is_complete_contiguous(const std::vector<std::uint32_t>& selected,
                            std::uint32_t available) noexcept {
  if (selected.size() != available) return false;
  for (std::uint32_t index = 0; index < available; ++index) {
    if (selected[index] != index) return false;
  }
  return true;
}

class PhysicalAsioControlApartment {
 public:
  PhysicalAsioControlApartment(const PhysicalAsioControlApartment&) = delete;
  PhysicalAsioControlApartment& operator=(
      const PhysicalAsioControlApartment&) = delete;

  static std::unique_ptr<PhysicalAsioControlApartment> create() noexcept {
    auto apartment = std::unique_ptr<PhysicalAsioControlApartment>(
        new (std::nothrow) PhysicalAsioControlApartment());
    if (!apartment) return {};
    try {
      apartment->thread_ = std::thread([self = apartment.get()] {
        self->run();
      });
    } catch (...) {
      return {};
    }
    std::unique_lock lock(apartment->mutex_);
    apartment->completed_.wait(lock, [&] { return apartment->ready_; });
    return apartment;
  }

  ~PhysicalAsioControlApartment() {
    {
      std::lock_guard submit_lock(submit_mutex_);
      std::lock_guard lock(mutex_);
      stopping_ = true;
      requested_.notify_one();
    }
    if (thread_.joinable()) thread_.join();
  }

  template <typename Context, typename Callable>
  void invoke(Context& context, Callable task) noexcept {
    struct Invocation {
      Context* context;
      Callable task;
    } invocation{&context, std::move(task)};
    std::lock_guard submit_lock(submit_mutex_);
    {
      std::lock_guard lock(mutex_);
      context_ = &invocation;
      task_ = [](void* opaque) noexcept {
        auto& value = *static_cast<Invocation*>(opaque);
        value.task(*value.context);
      };
      pending_ = true;
    }
    requested_.notify_one();
    std::unique_lock lock(mutex_);
    completed_.wait(lock, [&] { return !pending_; });
  }

 private:
  using Task = void (*)(void*) noexcept;

  PhysicalAsioControlApartment() = default;

  void run() noexcept {
    {
      std::lock_guard lock(mutex_);
      ready_ = true;
    }
    completed_.notify_all();
    for (;;) {
      Task task = nullptr;
      void* context = nullptr;
      {
        std::unique_lock lock(mutex_);
        requested_.wait(lock, [&] { return pending_ || stopping_; });
        if (stopping_ && !pending_) return;
        task = task_;
        context = context_;
      }
      task(context);
      {
        std::lock_guard lock(mutex_);
        pending_ = false;
        task_ = nullptr;
        context_ = nullptr;
      }
      completed_.notify_all();
    }
  }

  std::thread thread_;
  std::mutex submit_mutex_;
  std::mutex mutex_;
  std::condition_variable requested_;
  std::condition_variable completed_;
  Task task_ = nullptr;
  void* context_ = nullptr;
  bool ready_ = false;
  bool pending_ = false;
  bool stopping_ = false;
};

class PhysicalAsioEngineRuntime final : public EngineAudioRuntime {
 public:
  PhysicalAsioEngineRuntime(
                            std::unique_ptr<PhysicalAsioControlApartment> apartment,
                            std::unique_ptr<WindowsPhysicalAsioRuntime> runtime,
                            std::uint64_t graph_version) noexcept
      : apartment_(std::move(apartment)), runtime_(std::move(runtime)),
        graph_version_(graph_version) {}

  ~PhysicalAsioEngineRuntime() override {
    struct Context {
      std::unique_ptr<WindowsPhysicalAsioRuntime>* runtime;
    } context{&runtime_};
    apartment_->invoke(context, [](Context& value) noexcept {
      value.runtime->reset();
    });
  }

  EngineAudioRuntimeResult start(std::uint32_t) override {
    struct Context {
      WindowsPhysicalAsioRuntime* runtime;
      WindowsPhysicalAsioRuntimeError error =
          WindowsPhysicalAsioRuntimeError::InvalidRequest;
    } context{runtime_.get()};
    apartment_->invoke(context, [](Context& value) noexcept {
      value.error = value.runtime->start();
    });
    const auto error = context.error;
    if (error == WindowsPhysicalAsioRuntimeError::None) {
      return EngineAudioRuntimeResult::success();
    }
    return EngineAudioRuntimeResult::failure({{
        windows_physical_asio_runtime_error_name(error),
        "Physical ASIO driver could not start.",
    }});
  }

  void stop() noexcept override {
    struct Context {
      WindowsPhysicalAsioRuntime* runtime;
    } context{runtime_.get()};
    apartment_->invoke(context, [](Context& value) noexcept {
      static_cast<void>(value.runtime->stop());
    });
  }

  bool running() const noexcept override {
    return summary().state == WindowsPhysicalAsioRuntimeState::Running;
  }

  std::uint64_t graph_version() const noexcept override {
    return graph_version_;
  }

  diagnostics::EngineDiagnostics diagnostics() const override {
    return summary().diagnostics;
  }

 private:
  WindowsPhysicalAsioRuntimeSummary summary() const noexcept {
    struct Context {
      WindowsPhysicalAsioRuntime* runtime;
      WindowsPhysicalAsioRuntimeSummary summary;
    } context{runtime_.get()};
    apartment_->invoke(context, [](Context& value) noexcept {
      value.summary = value.runtime->summary();
    });
    return context.summary;
  }

  std::unique_ptr<PhysicalAsioControlApartment> apartment_;
  std::unique_ptr<WindowsPhysicalAsioRuntime> runtime_;
  std::uint64_t graph_version_ = 0;
};

}  // namespace

std::shared_ptr<graph::Graph> build_windows_physical_asio_direct_graph(
    const control::AudioRuntimeConfiguration& configuration,
    const platform::WindowsAsioDriverProbe& driver,
    std::uint64_t graph_version) {
  const auto channels = std::max(driver.input_channels,
                                 driver.output_channels);
  if (channels == 0 || configuration.physical_asio_sample_rate == 0 ||
      configuration.physical_asio_block_frames == 0) {
    return {};
  }
  try {
    auto graph = std::make_shared<graph::Graph>(
        graph_version, channels, configuration.physical_asio_block_frames,
        configuration.physical_asio_sample_rate);
    graph->add_node(std::make_unique<graph::PassthroughNode>());
    return graph;
  } catch (...) {
    return {};
  }
}

EngineAudioRuntimeBuildResult open_windows_physical_asio_engine_runtime(
    const control::AudioRuntimeConfiguration& configuration,
    std::shared_ptr<graph::Graph> graph,
    WindowsPhysicalAsioProbe probe,
    platform::WindowsAsioDriverActivator& activator,
    platform::WindowsAsioDriverNegotiator& negotiator) {
  if (configuration.mode != control::AudioRuntimeMode::PhysicalAsio ||
      !graph || !probe) {
    return failure("physical_asio_invalid_request",
                   "Physical ASIO runtime configuration is incomplete.");
  }
  auto apartment = PhysicalAsioControlApartment::create();
  if (!apartment) {
    return failure("physical_asio_control_apartment_unavailable",
                   "Physical ASIO control thread could not be created.");
  }
  struct ProbeContext {
    WindowsPhysicalAsioProbe* probe;
    const std::string* clsid;
    std::optional<platform::WindowsAsioDriverProbeResult> result;
  } probe_context{&probe, &configuration.physical_asio_driver_clsid};
  apartment->invoke(probe_context, [](ProbeContext& value) noexcept {
    try {
      value.result.emplace((*value.probe)(*value.clsid));
    } catch (...) {
    }
  });
  if (!probe_context.result) {
    return failure("physical_asio_driver_probe_failed",
                   "Physical ASIO driver inspection raised an exception.");
  }
  const auto& probed = *probe_context.result;
  if (!probed.ok()) {
    return failure("physical_asio_driver_probe_failed",
                   "Physical ASIO driver could not be inspected.");
  }
  const auto& driver = probed.probe();
  if (!is_complete_contiguous(configuration.physical_asio_input_channels,
                              driver.input_channels) ||
      !is_complete_contiguous(configuration.physical_asio_output_channels,
                              driver.output_channels)) {
    return failure(
        "physical_asio_channel_subset_not_implemented",
        "This alpha requires selecting every driver channel in native order; "
        "sparse Physical ASIO channel mapping is not implemented yet.");
  }
  auto direct_graph = build_windows_physical_asio_direct_graph(
      configuration, driver, graph->version());
  if (!direct_graph) {
    return failure(
        "physical_asio_direct_graph_build_failed",
        "Physical ASIO direct-I/O graph could not be created.");
  }

  platform::WindowsAsioControlOpenRequest request;
  request.driver = driver;
  request.sample_rate = configuration.physical_asio_sample_rate;
  request.preferred_block_frames = configuration.physical_asio_block_frames;
  struct OpenContext {
    std::shared_ptr<graph::Graph> graph;
    platform::WindowsAsioControlOpenRequest request;
    platform::WindowsAsioDriverActivator* activator;
    platform::WindowsAsioDriverNegotiator* negotiator;
    WindowsPhysicalAsioRuntimeOpenResult opened;
  } open_context{direct_graph, std::move(request), &activator, &negotiator};
  apartment->invoke(open_context, [](OpenContext& value) noexcept {
    value.opened = WindowsPhysicalAsioRuntime::open(
        std::move(value.graph), std::move(value.request), *value.activator,
        *value.negotiator);
  });
  auto opened = std::move(open_context.opened);
  if (!opened.ok()) {
    return failure(
        std::string("physical_asio_") +
            windows_physical_asio_runtime_error_name(opened.error),
        std::string("Physical ASIO runtime open failed: ") +
            platform::windows_asio_control_open_error_name(
                opened.control_open_error));
  }
  const auto version = direct_graph->version();
  return EngineAudioRuntimeBuildResult::success(
      std::make_unique<PhysicalAsioEngineRuntime>(std::move(apartment),
                                                  std::move(opened.runtime),
                                                  version));
}

EngineAudioRuntimeBuildResult open_windows_physical_asio_engine_runtime(
    const control::AudioRuntimeConfiguration& configuration,
    std::shared_ptr<graph::Graph> graph) {
  auto activator = platform::make_windows_asio_driver_activator();
  auto negotiator = platform::make_windows_asio_driver_negotiator();
  if (!activator || !negotiator) {
    return failure("physical_asio_runtime_resources_unavailable",
                   "Physical ASIO runtime dependencies could not be created.");
  }
  return open_windows_physical_asio_engine_runtime(
      configuration, std::move(graph), platform::probe_windows_asio_driver,
      *activator, *negotiator);
}

}  // namespace sar::service
