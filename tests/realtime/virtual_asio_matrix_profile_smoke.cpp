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
  return 0;
}
