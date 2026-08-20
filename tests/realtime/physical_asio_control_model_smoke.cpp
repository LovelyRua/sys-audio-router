#include "core/control/control_command.h"

#include <algorithm>
#include <cassert>
#include <string_view>

namespace {

bool has_error(const std::vector<sar::control::PresetError>& errors,
               std::string_view code) {
  return std::ranges::any_of(errors, [&](const auto& error) {
    return error.code == code;
  });
}

sar::control::AudioRuntimeConfiguration valid_configuration() {
  sar::control::AudioRuntimeConfiguration configuration;
  configuration.mode = sar::control::AudioRuntimeMode::PhysicalAsio;
  configuration.physical_asio_driver_clsid =
      "{12345678-1234-1234-1234-1234567890AB}";
  configuration.physical_asio_sample_rate = 48000;
  configuration.physical_asio_block_frames = 128;
  configuration.physical_asio_input_channels = {0, 2};
  configuration.physical_asio_output_channels = {1, 3};
  return configuration;
}

}  // namespace

int main() {
  const auto valid = valid_configuration();
  assert(sar::control::validate_audio_runtime_configuration(valid, false)
             .empty());

  auto capture_only = valid;
  capture_only.physical_asio_output_channels.clear();
  assert(sar::control::validate_audio_runtime_configuration(capture_only, false)
             .empty());

  auto render_only = valid;
  render_only.physical_asio_input_channels.clear();
  assert(sar::control::validate_audio_runtime_configuration(render_only, false)
             .empty());

  auto empty = valid;
  empty.physical_asio_input_channels.clear();
  empty.physical_asio_output_channels.clear();
  assert(has_error(sar::control::validate_audio_runtime_configuration(empty, false),
                   "empty_physical_asio_channel_selection"));

  auto duplicate = valid;
  duplicate.physical_asio_input_channels = {1, 1};
  assert(has_error(
      sar::control::validate_audio_runtime_configuration(duplicate, false),
      "duplicate_physical_asio_input_channel"));

  auto out_of_range = valid;
  out_of_range.physical_asio_output_channels = {
      static_cast<std::uint32_t>(sar::control::kMaximumPhysicalAsioChannels)};
  assert(has_error(
      sar::control::validate_audio_runtime_configuration(out_of_range, false),
      "physical_asio_channel_out_of_range"));

  auto invalid_rate = valid;
  invalid_rate.physical_asio_sample_rate = 0;
  assert(has_error(
      sar::control::validate_audio_runtime_configuration(invalid_rate, false),
      "invalid_physical_asio_sample_rate"));

  sar::control::AudioRuntimeConfiguration wasapi;
  wasapi.mode = sar::control::AudioRuntimeMode::WasapiRender;
  wasapi.physical_asio_driver_clsid = valid.physical_asio_driver_clsid;
  assert(has_error(
      sar::control::validate_audio_runtime_configuration(wasapi, false),
      "unexpected_physical_asio_configuration"));
}
