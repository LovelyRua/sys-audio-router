#include "core/service/windows_physical_asio_engine_runtime.h"

#include <cassert>
#include <memory>
#include <string>

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
}
