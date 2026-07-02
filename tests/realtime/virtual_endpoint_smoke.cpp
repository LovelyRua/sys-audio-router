#include "core/platform/virtual_endpoint.h"

#include <iostream>
#include <string>

namespace {

bool has_error_code(const sar::platform::VirtualEndpointResult& result,
                    const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::platform::VirtualEndpointDescriptor make_endpoint(std::string id,
                                                       std::string label) {
  sar::platform::VirtualEndpointDescriptor endpoint;
  endpoint.id = std::move(id);
  endpoint.label = std::move(label);
  endpoint.backend = sar::platform::AudioBackendKind::VirtualAsio;
  endpoint.direction = sar::platform::AudioDeviceDirection::Duplex;
  endpoint.format = {
      .sample_rate = 48000,
      .channels = 16,
      .frames_per_block = 128,
  };
  return endpoint;
}

}  // namespace

int main() {
  {
    sar::platform::VirtualEndpointRegistry registry;
    auto result = registry.add_endpoint(make_endpoint("sar_asio_1", "SAR ASIO 1"));
    if (const auto failure = expect(result.ok(), "Expected endpoint add success")) {
      return failure;
    }
    if (const auto failure = expect(registry.endpoints().size() == 1,
                                    "Expected one virtual endpoint")) {
      return failure;
    }

    const auto devices = registry.list_devices();
    if (const auto failure = expect(devices.ok(), "Expected endpoint device list success")) {
      return failure;
    }
    if (const auto failure = expect(devices.devices().size() == 1,
                                    "Expected one virtual audio device")) {
      return failure;
    }
    if (const auto failure = expect(devices.devices()[0].is_virtual,
                                    "Expected virtual audio device flag")) {
      return failure;
    }

    result = registry.remove_endpoint("sar_asio_1");
    if (const auto failure = expect(result.ok(), "Expected endpoint remove success")) {
      return failure;
    }
    if (const auto failure = expect(registry.endpoints().empty(),
                                    "Expected endpoint to be removed")) {
      return failure;
    }
  }

  {
    sar::platform::VirtualEndpointRegistry registry;
    auto result = registry.add_endpoint(make_endpoint("dup", "Endpoint A"));
    if (const auto failure = expect(result.ok(), "Expected first duplicate add success")) {
      return failure;
    }
    result = registry.add_endpoint(make_endpoint("dup", "Endpoint B"));
    if (const auto failure = expect(!result.ok(), "Expected duplicate endpoint failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "duplicate_endpoint_id"),
                                    "Expected duplicate_endpoint_id error")) {
      return failure;
    }
  }

  {
    auto endpoint = make_endpoint("", "");
    endpoint.backend = sar::platform::AudioBackendKind::Wasapi;
    endpoint.format.sample_rate = 0;
    endpoint.format.channels = 0;
    endpoint.format.frames_per_block = 0;

    sar::platform::VirtualEndpointRegistry registry;
    const auto result = registry.add_endpoint(endpoint);
    if (const auto failure = expect(!result.ok(), "Expected invalid endpoint failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_endpoint_id"),
                                    "Expected empty_endpoint_id error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_endpoint_label"),
                                    "Expected empty_endpoint_label error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_virtual_backend"),
                                    "Expected invalid_virtual_backend error")) {
      return failure;
    }
  }

  {
    sar::platform::VirtualEndpointRegistry registry;
    const auto result = registry.remove_endpoint("missing");
    if (const auto failure = expect(!result.ok(), "Expected unknown endpoint remove failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "unknown_endpoint_id"),
                                    "Expected unknown_endpoint_id error")) {
      return failure;
    }
  }

  std::cout << "Virtual endpoint smoke test passed\n";
  return 0;
}
