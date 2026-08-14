#include "core/service/virtual_asio_instance_layout.h"

#include <cassert>
#include <vector>

int main() {
  using sar::control::VirtualAsioDeviceDefinition;
  std::vector<VirtualAsioDeviceDefinition> definitions = {
      {.device_id = "main", .input_channels = 4, .output_channels = 2},
      {.device_id = "disabled",
       .input_channels = 32,
       .output_channels = 32,
       .enabled = false},
      {.device_id = "aux", .input_channels = 2, .output_channels = 4},
  };
  const sar::service::VirtualAsioMatrixProfile profile{2, 3, 6};
  const auto layout =
      sar::service::virtual_asio_instance_layout(definitions, profile);
  assert(layout.has_value());
  assert(layout->instances.size() == 2);
  assert(layout->output_channels == 6);
  assert(layout->input_channels == 6);
  assert(layout->instances[0].definition_index == 0);
  assert(layout->instances[0].daw_output_offset == 0);
  assert(layout->instances[0].daw_input_offset == 0);
  assert(layout->instances[0].output_channels == 2);
  assert(layout->instances[0].input_channels == 4);
  assert(layout->instances[1].definition_index == 2);
  assert(layout->instances[1].daw_output_offset == 2);
  assert(layout->instances[1].daw_input_offset == 4);
  assert(layout->instances[1].output_channels == 4);
  assert(layout->instances[1].input_channels == 2);

  definitions[2].output_channels = 3;
  assert(!sar::service::virtual_asio_instance_layout(definitions, profile)
              .has_value());
  definitions[2].output_channels = 4;
  definitions[0].enabled = false;
  assert(!sar::service::virtual_asio_instance_layout(definitions, profile)
              .has_value());
}
