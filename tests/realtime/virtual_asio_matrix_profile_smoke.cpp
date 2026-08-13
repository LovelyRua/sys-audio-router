#include "core/service/virtual_asio_matrix_profile.h"

#include <cassert>

int main() {
  sar::control::PresetRouteMatrix matrix;
  matrix.inputs = {{"capture-1", "Capture 1"},
                   {"asio-output-1", "DAW Out 1"},
                   {"asio-output-2", "DAW Out 2"},
                   {"asio-output-3", "DAW Out 3"},
                   {"asio-output-4", "DAW Out 4"}};
  matrix.outputs = {{"render-1", "Render 1"},
                    {"render-2", "Render 2"},
                    {"asio-input-1", "DAW In 1"},
                    {"asio-input-2", "DAW In 2"},
                    {"asio-input-3", "DAW In 3"},
                    {"asio-input-4", "DAW In 4"}};
  const auto profile = sar::service::virtual_asio_matrix_profile(matrix);
  assert(profile.has_value());
  assert(profile->daw_output_offset == 1);
  assert(profile->daw_input_offset == 2);
  assert(profile->channels == 4);

  matrix.inputs.insert(matrix.inputs.begin() + 3,
                       {"capture-2", "Capture 2"});
  assert(!sar::service::virtual_asio_matrix_profile(matrix).has_value());

  sar::control::PresetDocument preset;
  preset.matrix.inputs = {{"capture-l", "Capture L"},
                          {"asio-output-l", "DAW Out L"},
                          {"asio-output-r", "DAW Out R"}};
  preset.matrix.outputs = {{"render-l", "Render L"},
                           {"asio-input-l", "DAW In L"},
                           {"asio-input-r", "DAW In R"}};
  preset.matrix.routes = {{"capture-l", "asio-input-l", 1.0F, false},
                          {"asio-output-l", "render-l", 1.0F, false}};
  assert(sar::service::resize_virtual_asio_matrix_profile(preset, 8));
  const auto resized =
      sar::service::virtual_asio_matrix_profile(preset.matrix);
  assert(resized.has_value() && resized->channels == 8);
  assert(preset.matrix.inputs[1].id == "asio-output-1");
  assert(preset.matrix.inputs[8].id == "asio-output-8");
  assert(preset.matrix.outputs[1].id == "asio-input-1");
  assert(preset.matrix.routes.empty());
  return 0;
}
