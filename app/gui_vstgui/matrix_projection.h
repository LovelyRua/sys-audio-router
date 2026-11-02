#pragma once

#include "core/control/control_command.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace sar::gui_vstgui {

struct MatrixProjection {
  control::PresetRouteMatrix matrix;
  std::vector<bool> inputActive;
  std::vector<bool> outputActive;
};

namespace matrix_projection_detail {

enum class EndpointFamily { Wasapi, Asio, Other };

[[nodiscard]] inline EndpointFamily endpointFamily(
    const graph::RouteEndpointDescriptor& endpoint, std::size_t index,
    std::size_t count) {
  auto identity = endpoint.id + " " + endpoint.label;
  std::transform(identity.begin(), identity.end(), identity.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  });
  const auto contains = [&identity](std::string_view text) {
    return identity.find(text) != std::string::npos;
  };
  if (contains("asio") || contains("daw") || contains("virtual")) {
    return EndpointFamily::Asio;
  }
  if (contains("wasapi") || contains("capture") || contains("render") ||
      contains("monitor") || contains("hardware")) {
    return EndpointFamily::Wasapi;
  }
  if (count == 4) {
    return index < 2 ? EndpointFamily::Wasapi : EndpointFamily::Asio;
  }
  return EndpointFamily::Other;
}

[[nodiscard]] inline bool positiveDecimal(std::string_view text) {
  return !text.empty() && text.front() >= '1' && text.front() <= '9' &&
         std::all_of(text.begin() + 1, text.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}

[[nodiscard]] inline std::string_view channelSuffix(std::string_view id) {
  const auto separator = id.rfind(".ch");
  if (separator == std::string_view::npos) {
    return {};
  }
  const auto suffix = id.substr(separator + 3);
  return positiveDecimal(suffix) ? suffix : std::string_view{};
}

[[nodiscard]] inline bool isLabSource(std::string_view endpointId,
                                      std::string_view suffix) {
  constexpr std::string_view prefix = "virtual-wasapi-lab-";
  const auto base = endpointId.substr(0, endpointId.size() - suffix.size() - 3);
  return base.starts_with(prefix) && positiveDecimal(base.substr(prefix.size()));
}

[[nodiscard]] inline bool hasRuntimeBinding(
    std::string_view endpointId, std::string_view suffix, bool source,
    const std::vector<control::AudioRuntimeEndpointConfiguration>& endpoints) {
  std::uint32_t channel = 0;
  const auto parsed =
      std::from_chars(suffix.data(), suffix.data() + suffix.size(), channel);
  if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size()) {
    return false;
  }
  const auto base = endpointId.substr(0, endpointId.size() - suffix.size() - 3);
  const auto direction = source ? control::AudioRuntimeEndpointDirection::Capture
                                : control::AudioRuntimeEndpointDirection::Render;
  return std::any_of(endpoints.begin(), endpoints.end(), [&](const auto& runtime) {
    return runtime.direction == direction && runtime.endpoint_id == base &&
           channel <= runtime.channel_count;
  });
}

inline void projectEndpoints(
    const std::vector<graph::RouteEndpointDescriptor>& endpoints, bool source,
    const control::AudioRuntimeConfiguration& runtime, bool runtimeConfigured,
    bool showInactive, std::vector<graph::RouteEndpointDescriptor>& projected,
    std::vector<bool>& activeFlags) {
  projected.reserve(endpoints.size());
  activeFlags.reserve(endpoints.size());
  for (std::size_t index = 0; index < endpoints.size(); ++index) {
    const auto& endpoint = endpoints[index];
    const auto family = endpointFamily(endpoint, index, endpoints.size());
    bool active = runtime.mode == control::AudioRuntimeMode::WasapiDuplex ||
                  runtime.mode == control::AudioRuntimeMode::WasapiMatrix ||
                  (source ? family != EndpointFamily::Wasapi
                          : family != EndpointFamily::Asio);
    if (runtime.mode == control::AudioRuntimeMode::WasapiMatrix &&
        runtimeConfigured) {
      const auto suffix = channelSuffix(endpoint.id);
      // Lab inputs are virtual sources, not runtime endpoint bindings.
      if (!suffix.empty() && !(source && isLabSource(endpoint.id, suffix)) &&
          !hasRuntimeBinding(endpoint.id, suffix, source, runtime.endpoints)) {
        active = false;
      }
    }
    if (active || showInactive) {
      projected.push_back(endpoint);
      activeFlags.push_back(active);
    }
  }
}

}  // namespace matrix_projection_detail

[[nodiscard]] inline MatrixProjection projectMatrix(
    const control::PresetRouteMatrix& matrix,
    const control::AudioRuntimeConfiguration& runtime, bool runtimeConfigured,
    bool showInactive) {
  MatrixProjection result;
  matrix_projection_detail::projectEndpoints(
      matrix.inputs, true, runtime, runtimeConfigured, showInactive,
      result.matrix.inputs, result.inputActive);
  matrix_projection_detail::projectEndpoints(
      matrix.outputs, false, runtime, runtimeConfigured, showInactive,
      result.matrix.outputs, result.outputActive);

  std::unordered_set<std::string_view> inputIds;
  std::unordered_set<std::string_view> outputIds;
  for (const auto& endpoint : result.matrix.inputs) {
    inputIds.insert(endpoint.id);
  }
  for (const auto& endpoint : result.matrix.outputs) {
    outputIds.insert(endpoint.id);
  }
  result.matrix.routes.reserve(matrix.routes.size());
  for (const auto& route : matrix.routes) {
    if (inputIds.contains(route.input_id) && outputIds.contains(route.output_id)) {
      result.matrix.routes.push_back(route);
    }
  }
  return result;
}

}  // namespace sar::gui_vstgui
