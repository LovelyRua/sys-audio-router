#include "core/service/engine_control_service.h"
#include "core/service/windows_wasapi_engine_runtime.h"

#include <cassert>
#include <type_traits>

static_assert(std::is_base_of_v<sar::platform::WasapiDuplexRuntime,
                                sar::platform::WindowsWasapiRenderLoop>);

namespace {

sar::control::PresetDocument make_preset() {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48000;
  preset.frames_per_block = 128;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"input-l", "Input L"});
  preset.matrix.inputs.push_back({"input-r", "Input R"});
  preset.matrix.outputs.push_back({"output-l", "Output L"});
  preset.matrix.outputs.push_back({"output-r", "Output R"});
  preset.matrix.routes.push_back({"input-l", "output-l", 1.0F, false});
  preset.matrix.routes.push_back({"input-r", "output-r", 1.0F, false});
  return preset;
}

}  // namespace

int main() {
  const auto null_render =
      sar::service::WindowsWasapiEngineRuntime::open_default_render(nullptr);
  assert(!null_render.ok());
  assert(null_render.errors()[0].code == "null_runtime_graph");

  const auto null_duplex =
      sar::service::WindowsWasapiEngineRuntime::open_default_duplex(nullptr);
  assert(!null_duplex.ok());
  assert(null_duplex.errors()[0].code == "null_runtime_graph");

  auto service_result = sar::service::EngineControlService::create(make_preset());
  assert(service_result.ok());
  auto service = service_result.take_service();
  auto default_duplex =
      sar::service::WindowsWasapiEngineRuntime::open_default_duplex(
          service->session().current_graph());
  assert(default_duplex.ok());
  auto default_runtime = default_duplex.take_runtime();
  assert(default_runtime->mode() ==
         sar::service::WindowsWasapiEngineRuntimeMode::Duplex);
  assert(!default_runtime->running());

  auto explicit_duplex =
      sar::service::WindowsWasapiEngineRuntime::open_duplex(
          "capture-id", "render-id", service->session().current_graph());
  assert(explicit_duplex.ok());
  assert(explicit_duplex.take_runtime()->mode() ==
         sar::service::WindowsWasapiEngineRuntimeMode::Duplex);

  const auto missing_capture =
      sar::service::WindowsWasapiEngineRuntime::open_duplex(
          {}, "render-id", service->session().current_graph());
  assert(!missing_capture.ok());
  assert(missing_capture.errors()[0].code == "missing_duplex_device_id");

  const auto missing_render =
      sar::service::WindowsWasapiEngineRuntime::open_duplex(
          "capture-id", {}, service->session().current_graph());
  assert(!missing_render.ok());
  assert(missing_render.errors()[0].code == "missing_duplex_device_id");

  const auto missing_explicit_render =
      sar::service::WindowsWasapiEngineRuntime::open_render(
          {}, service->session().current_graph());
  assert(!missing_explicit_render.ok());
  assert(missing_explicit_render.errors()[0].code ==
         "missing_render_device_id");
}
