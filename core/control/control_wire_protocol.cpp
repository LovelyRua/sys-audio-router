#include "core/control/control_wire_protocol.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace sar::control {

namespace {

constexpr std::uint32_t kMagic = 0x31524153U;  // SAR1
constexpr std::uint16_t kCommandKind = 1;
constexpr std::uint16_t kResponseKind = 2;

class Writer {
 public:
  explicit Writer(std::uint16_t kind) : kind_(kind) {
    bytes_.resize(kControlWireHeaderSize);
  }

  template <typename T>
  void scalar(T value) {
    static_assert(std::is_integral_v<T>);
    using Unsigned = std::make_unsigned_t<T>;
    auto bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      bytes_.push_back(static_cast<std::uint8_t>((bits >> (index * 8U)) & 0xFFU));
    }
  }

  void boolean(bool value) { scalar<std::uint8_t>(value ? 1U : 0U); }
  void floating(float value) { scalar(std::bit_cast<std::uint32_t>(value)); }
  void floating(double value) { scalar(std::bit_cast<std::uint64_t>(value)); }

  void string(const std::string& value) {
    if (value.size() > kControlWireMaxStringBytes ||
        value.size() > std::numeric_limits<std::uint32_t>::max()) {
      fail(ControlWireErrorCode::StringTooLong);
      return;
    }
    scalar(static_cast<std::uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  void count(std::size_t value) {
    if (value > kControlWireMaxArrayElements ||
        value > std::numeric_limits<std::uint32_t>::max()) {
      fail(ControlWireErrorCode::ArrayTooLong);
      return;
    }
    scalar(static_cast<std::uint32_t>(value));
  }

  [[nodiscard]] ControlWireEncodeResult finish() {
    if (error_.code != ControlWireErrorCode::None) {
      return {{}, error_};
    }
    if (bytes_.size() > kControlWireMaxMessageBytes) {
      return {{}, {ControlWireErrorCode::MessageTooLarge, bytes_.size()}};
    }
    const auto payload_size =
        static_cast<std::uint32_t>(bytes_.size() - kControlWireHeaderSize);
    write_header(0, kMagic);
    write_header(4, kControlWireVersion);
    write_header(6, kind_);
    write_header(8, payload_size);
    return {std::move(bytes_), {}};
  }

 private:
  template <typename T>
  void write_header(std::size_t offset, T value) {
    using Unsigned = std::make_unsigned_t<T>;
    auto bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      bytes_[offset + index] =
          static_cast<std::uint8_t>((bits >> (index * 8U)) & 0xFFU);
    }
  }

  void fail(ControlWireErrorCode code) {
    if (error_.code == ControlWireErrorCode::None) {
      error_ = {code, bytes_.size()};
    }
  }

  std::uint16_t kind_;
  std::vector<std::uint8_t> bytes_;
  ControlWireError error_;
};

class Reader {
 public:
  Reader(std::span<const std::uint8_t> bytes, std::uint16_t expected_kind)
      : bytes_(bytes) {
    if (bytes.size() > kControlWireMaxMessageBytes) {
      fail(ControlWireErrorCode::MessageTooLarge);
      return;
    }
    if (bytes.size() < kControlWireHeaderSize) {
      fail(ControlWireErrorCode::Truncated);
      return;
    }
    const auto magic = scalar<std::uint32_t>();
    const auto version = scalar<std::uint16_t>();
    const auto kind = scalar<std::uint16_t>();
    const auto payload_size = scalar<std::uint32_t>();
    if (!ok()) {
      return;
    }
    if (magic != kMagic) {
      fail(ControlWireErrorCode::InvalidMagic);
    } else if (version != kControlWireVersion) {
      fail(ControlWireErrorCode::UnsupportedVersion);
    } else if (kind != expected_kind) {
      fail(ControlWireErrorCode::UnexpectedMessageKind);
    } else if (payload_size != bytes.size() - kControlWireHeaderSize) {
      fail(ControlWireErrorCode::InvalidLength);
    }
  }

  template <typename T>
  T scalar() {
    static_assert(std::is_integral_v<T>);
    if (!require(sizeof(T))) {
      return {};
    }
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      value |= static_cast<Unsigned>(bytes_[offset_ + index]) << (index * 8U);
    }
    offset_ += sizeof(T);
    return static_cast<T>(value);
  }

  bool boolean() {
    const auto value = scalar<std::uint8_t>();
    if (ok() && value > 1) {
      fail(ControlWireErrorCode::InvalidBoolean);
    }
    return value == 1;
  }

  float float32() {
    const auto value = std::bit_cast<float>(scalar<std::uint32_t>());
    if (ok() && !std::isfinite(value)) {
      fail(ControlWireErrorCode::InvalidValue);
    }
    return value;
  }

  double float64() {
    const auto value = std::bit_cast<double>(scalar<std::uint64_t>());
    if (ok() && !std::isfinite(value)) {
      fail(ControlWireErrorCode::InvalidValue);
    }
    return value;
  }

  std::string string() {
    const auto length = scalar<std::uint32_t>();
    if (!ok()) {
      return {};
    }
    if (length > kControlWireMaxStringBytes) {
      fail(ControlWireErrorCode::StringTooLong);
      return {};
    }
    if (!require(length)) {
      return {};
    }
    std::string result(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
    offset_ += length;
    return result;
  }

  std::uint32_t count() {
    const auto value = scalar<std::uint32_t>();
    if (ok() && value > kControlWireMaxArrayElements) {
      fail(ControlWireErrorCode::ArrayTooLong);
    }
    return value;
  }

  template <typename Enum>
  Enum enumeration(std::uint32_t maximum) {
    const auto value = scalar<std::uint32_t>();
    if (ok() && value > maximum) {
      fail(ControlWireErrorCode::UnknownEnum);
    }
    return static_cast<Enum>(value);
  }

  void finish() {
    if (ok() && offset_ != bytes_.size()) {
      fail(ControlWireErrorCode::TrailingBytes);
    }
  }

  [[nodiscard]] bool ok() const noexcept {
    return error_.code == ControlWireErrorCode::None;
  }
  [[nodiscard]] ControlWireError error() const noexcept { return error_; }

 private:
  bool require(std::size_t size) {
    if (!ok()) {
      return false;
    }
    if (size > bytes_.size() - offset_) {
      fail(ControlWireErrorCode::Truncated);
      return false;
    }
    return true;
  }

  void fail(ControlWireErrorCode code) {
    if (error_.code == ControlWireErrorCode::None) {
      error_ = {code, offset_};
    }
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t offset_ = 0;
  ControlWireError error_;
};

void encode_endpoint(Writer& writer, const graph::RouteEndpointDescriptor& endpoint) {
  writer.string(endpoint.id);
  writer.string(endpoint.label);
}

graph::RouteEndpointDescriptor decode_endpoint(Reader& reader) {
  return {reader.string(), reader.string()};
}

void encode_preset(Writer& writer, const PresetDocument& preset) {
  writer.scalar(preset.schema_version);
  writer.scalar(preset.sample_rate);
  writer.scalar(preset.frames_per_block);
  writer.count(preset.nodes.size());
  for (const auto& node : preset.nodes) {
    writer.string(node.id);
    writer.string(node.label);
    writer.string(node.type);
  }
  writer.count(preset.matrix.inputs.size());
  for (const auto& endpoint : preset.matrix.inputs) {
    encode_endpoint(writer, endpoint);
  }
  writer.count(preset.matrix.outputs.size());
  for (const auto& endpoint : preset.matrix.outputs) {
    encode_endpoint(writer, endpoint);
  }
  writer.count(preset.matrix.routes.size());
  for (const auto& route : preset.matrix.routes) {
    writer.string(route.input_id);
    writer.string(route.output_id);
    writer.floating(route.gain);
    writer.boolean(route.muted);
  }
}

PresetDocument decode_preset(Reader& reader) {
  PresetDocument preset;
  preset.schema_version = reader.scalar<std::uint32_t>();
  preset.sample_rate = reader.scalar<std::uint32_t>();
  preset.frames_per_block = reader.scalar<std::uint32_t>();
  const auto node_count = reader.count();
  if (!reader.ok()) {
    return preset;
  }
  preset.nodes.reserve(node_count);
  for (std::uint32_t index = 0; index < node_count && reader.ok(); ++index) {
    preset.nodes.push_back({reader.string(), reader.string(), reader.string()});
  }
  const auto input_count = reader.count();
  if (!reader.ok()) {
    return preset;
  }
  preset.matrix.inputs.reserve(input_count);
  for (std::uint32_t index = 0; index < input_count && reader.ok(); ++index) {
    preset.matrix.inputs.push_back(decode_endpoint(reader));
  }
  const auto output_count = reader.count();
  if (!reader.ok()) {
    return preset;
  }
  preset.matrix.outputs.reserve(output_count);
  for (std::uint32_t index = 0; index < output_count && reader.ok(); ++index) {
    preset.matrix.outputs.push_back(decode_endpoint(reader));
  }
  const auto route_count = reader.count();
  if (!reader.ok()) {
    return preset;
  }
  preset.matrix.routes.reserve(route_count);
  for (std::uint32_t index = 0; index < route_count && reader.ok(); ++index) {
    PresetRoute route;
    route.input_id = reader.string();
    route.output_id = reader.string();
    route.gain = reader.float32();
    route.muted = reader.boolean();
    preset.matrix.routes.push_back(std::move(route));
  }
  return preset;
}

void encode_diagnostics(Writer& writer,
                        const diagnostics::EngineDiagnostics& diagnostics) {
  writer.scalar(diagnostics.graph_version);
  writer.scalar(diagnostics.processed_blocks);
  writer.scalar(diagnostics.xrun_count);
  writer.scalar(diagnostics.capture_fifo_fill_frames);
  writer.scalar(diagnostics.render_fifo_fill_frames);
  writer.scalar(diagnostics.capture_fifo_overflow_cycles);
  writer.scalar(diagnostics.capture_fifo_overflow_frames);
  writer.scalar(diagnostics.render_fifo_overflow_cycles);
  writer.scalar(diagnostics.render_fifo_overflow_frames);
  writer.scalar(diagnostics.render_fifo_underflow_cycles);
  writer.scalar(diagnostics.render_fifo_underflow_frames);
  writer.scalar(diagnostics.virtual_asio_pushed_blocks);
  writer.scalar(diagnostics.virtual_asio_dropped_blocks);
  writer.scalar(diagnostics.virtual_asio_consumed_blocks);
  writer.scalar(diagnostics.virtual_asio_mixed_blocks);
  writer.scalar(diagnostics.virtual_asio_silent_reads);
  writer.scalar(diagnostics.virtual_asio_clipped_samples);
  writer.scalar(diagnostics.virtual_asio_non_finite_samples);
  writer.scalar(diagnostics.virtual_asio_maximum_queue_depth);
  writer.scalar(diagnostics.virtual_asio_active_producers);
  writer.scalar(diagnostics.sample_conversion_import_failures);
  writer.scalar(diagnostics.sample_conversion_export_failures);
  writer.floating(diagnostics.last_callback_seconds);
  writer.floating(diagnostics.peak_callback_seconds);
  writer.floating(diagnostics.virtual_asio_peak);
}

diagnostics::EngineDiagnostics decode_diagnostics(Reader& reader) {
  diagnostics::EngineDiagnostics diagnostics;
  diagnostics.graph_version = reader.scalar<std::uint64_t>();
  diagnostics.processed_blocks = reader.scalar<std::uint64_t>();
  diagnostics.xrun_count = reader.scalar<std::uint64_t>();
  diagnostics.capture_fifo_fill_frames = reader.scalar<std::uint64_t>();
  diagnostics.render_fifo_fill_frames = reader.scalar<std::uint64_t>();
  diagnostics.capture_fifo_overflow_cycles = reader.scalar<std::uint64_t>();
  diagnostics.capture_fifo_overflow_frames = reader.scalar<std::uint64_t>();
  diagnostics.render_fifo_overflow_cycles = reader.scalar<std::uint64_t>();
  diagnostics.render_fifo_overflow_frames = reader.scalar<std::uint64_t>();
  diagnostics.render_fifo_underflow_cycles = reader.scalar<std::uint64_t>();
  diagnostics.render_fifo_underflow_frames = reader.scalar<std::uint64_t>();
  diagnostics.virtual_asio_pushed_blocks = reader.scalar<std::uint64_t>();
  diagnostics.virtual_asio_dropped_blocks = reader.scalar<std::uint64_t>();
  diagnostics.virtual_asio_consumed_blocks = reader.scalar<std::uint64_t>();
  diagnostics.virtual_asio_mixed_blocks = reader.scalar<std::uint64_t>();
  diagnostics.virtual_asio_silent_reads = reader.scalar<std::uint64_t>();
  diagnostics.virtual_asio_clipped_samples = reader.scalar<std::uint64_t>();
  diagnostics.virtual_asio_non_finite_samples = reader.scalar<std::uint64_t>();
  diagnostics.virtual_asio_maximum_queue_depth = reader.scalar<std::uint64_t>();
  diagnostics.virtual_asio_active_producers = reader.scalar<std::uint64_t>();
  diagnostics.sample_conversion_import_failures = reader.scalar<std::uint64_t>();
  diagnostics.sample_conversion_export_failures = reader.scalar<std::uint64_t>();
  diagnostics.last_callback_seconds = reader.float64();
  diagnostics.peak_callback_seconds = reader.float64();
  diagnostics.virtual_asio_peak = reader.float64();
  return diagnostics;
}

void encode_device(Writer& writer, const platform::AudioDeviceDescriptor& device) {
  writer.string(device.id);
  writer.string(device.label);
  writer.scalar(static_cast<std::uint32_t>(device.backend));
  writer.scalar(static_cast<std::uint32_t>(device.direction));
  writer.count(device.formats.size());
  for (const auto& format : device.formats) {
    writer.scalar(format.sample_rate);
    writer.scalar(format.channels);
    writer.scalar(format.frames_per_block);
    writer.scalar(format.bits_per_sample);
    writer.scalar(format.valid_bits_per_sample);
    writer.scalar(static_cast<std::uint32_t>(format.sample_format));
  }
  writer.boolean(device.is_default);
  writer.boolean(device.is_virtual);
}

platform::AudioDeviceDescriptor decode_device(Reader& reader) {
  platform::AudioDeviceDescriptor device;
  device.id = reader.string();
  device.label = reader.string();
  device.backend = reader.enumeration<platform::AudioBackendKind>(5);
  device.direction = reader.enumeration<platform::AudioDeviceDirection>(2);
  const auto format_count = reader.count();
  if (!reader.ok()) {
    return device;
  }
  device.formats.reserve(format_count);
  for (std::uint32_t index = 0; index < format_count && reader.ok(); ++index) {
    platform::AudioFormat format;
    format.sample_rate = reader.scalar<std::uint32_t>();
    format.channels = reader.scalar<std::uint32_t>();
    format.frames_per_block = reader.scalar<std::uint32_t>();
    format.bits_per_sample = reader.scalar<std::uint32_t>();
    format.valid_bits_per_sample = reader.scalar<std::uint32_t>();
    format.sample_format = reader.enumeration<platform::AudioSampleFormat>(2);
    device.formats.push_back(format);
  }
  device.is_default = reader.boolean();
  device.is_virtual = reader.boolean();
  return device;
}

}  // namespace

ControlWireEncodeResult encode_control_command(const ControlCommand& command) {
  Writer writer(kCommandKind);
  writer.scalar(command.schema_version);
  writer.string(command.command_id);
  writer.scalar(static_cast<std::uint32_t>(command.type));
  writer.string(command.endpoint_id);
  writer.string(command.endpoint_label);
  writer.string(command.input_id);
  writer.string(command.output_id);
  writer.floating(command.gain);
  writer.boolean(command.mute);
  encode_preset(writer, command.preset);
  writer.scalar(static_cast<std::uint32_t>(command.audio_runtime.mode));
  writer.string(command.audio_runtime.capture_device_id);
  writer.string(command.audio_runtime.render_device_id);
  return writer.finish();
}

ControlCommandDecodeResult decode_control_command(
    std::span<const std::uint8_t> bytes) {
  Reader reader(bytes, kCommandKind);
  ControlCommand command;
  if (!reader.ok()) {
    return {std::move(command), reader.error()};
  }
  command.schema_version = reader.scalar<std::uint32_t>();
  command.command_id = reader.string();
  command.type = reader.enumeration<ControlCommandType>(
      static_cast<std::uint32_t>(ControlCommandType::ConfigureAudioRuntime));
  command.endpoint_id = reader.string();
  command.endpoint_label = reader.string();
  command.input_id = reader.string();
  command.output_id = reader.string();
  command.gain = reader.float32();
  command.mute = reader.boolean();
  command.preset = decode_preset(reader);
  command.audio_runtime.mode = reader.enumeration<AudioRuntimeMode>(2);
  command.audio_runtime.capture_device_id = reader.string();
  command.audio_runtime.render_device_id = reader.string();
  reader.finish();
  return {std::move(command), reader.error()};
}

ControlWireEncodeResult encode_control_response(const ControlResponse& response) {
  Writer writer(kResponseKind);
  writer.string(response.command_id);
  writer.scalar(static_cast<std::uint32_t>(response.status));
  writer.count(response.errors.size());
  for (const auto& error : response.errors) {
    writer.string(error.code);
    writer.string(error.message);
  }
  writer.boolean(response.has_preset);
  if (response.has_preset) {
    encode_preset(writer, response.preset);
  }
  writer.boolean(response.has_diagnostics);
  if (response.has_diagnostics) {
    encode_diagnostics(writer, response.diagnostics);
  }
  writer.boolean(response.has_devices);
  if (response.has_devices) {
    writer.count(response.devices.size());
    for (const auto& device : response.devices) {
      encode_device(writer, device);
    }
  }
  writer.scalar(response.next_graph_version);
  writer.boolean(response.has_session_state);
  writer.boolean(response.has_active_graph);
  if (response.has_active_graph) {
    writer.scalar(response.active_graph.version);
    writer.scalar(response.active_graph.sample_rate);
    writer.scalar(static_cast<std::uint64_t>(response.active_graph.channels));
    writer.scalar(static_cast<std::uint64_t>(response.active_graph.frames));
    writer.count(response.active_graph.nodes.size());
    for (const auto& node : response.active_graph.nodes) {
      writer.string(node.id);
      writer.string(node.label);
    }
  }
  writer.boolean(response.has_audio_runtime_state);
  if (response.has_audio_runtime_state) {
    writer.boolean(response.audio_runtime.installed);
    writer.boolean(response.audio_runtime.running);
    writer.scalar(response.audio_runtime.graph_version);
    writer.boolean(response.audio_runtime.configured);
    writer.scalar(static_cast<std::uint32_t>(
        response.audio_runtime.configuration.mode));
    writer.string(response.audio_runtime.configuration.capture_device_id);
    writer.string(response.audio_runtime.configuration.render_device_id);
  }
  return writer.finish();
}

ControlResponseDecodeResult decode_control_response(
    std::span<const std::uint8_t> bytes) {
  Reader reader(bytes, kResponseKind);
  ControlResponse response;
  if (!reader.ok()) {
    return {std::move(response), reader.error()};
  }
  response.command_id = reader.string();
  response.status = reader.enumeration<ControlResponseStatus>(1);
  const auto error_count = reader.count();
  if (reader.ok()) {
    response.errors.reserve(error_count);
    for (std::uint32_t index = 0; index < error_count && reader.ok(); ++index) {
      response.errors.push_back({reader.string(), reader.string()});
    }
  }
  response.has_preset = reader.boolean();
  if (reader.ok() && response.has_preset) {
    response.preset = decode_preset(reader);
  }
  response.has_diagnostics = reader.boolean();
  if (reader.ok() && response.has_diagnostics) {
    response.diagnostics = decode_diagnostics(reader);
  }
  response.has_devices = reader.boolean();
  if (reader.ok() && response.has_devices) {
    const auto device_count = reader.count();
    if (reader.ok()) {
      response.devices.reserve(device_count);
      for (std::uint32_t index = 0; index < device_count && reader.ok(); ++index) {
        response.devices.push_back(decode_device(reader));
      }
    }
  }
  response.next_graph_version = reader.scalar<std::uint64_t>();
  response.has_session_state = reader.boolean();
  response.has_active_graph = reader.boolean();
  if (reader.ok() && response.has_active_graph) {
    response.active_graph.version = reader.scalar<std::uint64_t>();
    response.active_graph.sample_rate = reader.scalar<std::uint32_t>();
    const auto channels = reader.scalar<std::uint64_t>();
    const auto frames = reader.scalar<std::uint64_t>();
    if (channels > std::numeric_limits<std::size_t>::max() ||
        frames > std::numeric_limits<std::size_t>::max()) {
      return {std::move(response), {ControlWireErrorCode::InvalidValue, 0}};
    }
    response.active_graph.channels = static_cast<std::size_t>(channels);
    response.active_graph.frames = static_cast<std::size_t>(frames);
    const auto node_count = reader.count();
    if (reader.ok()) {
      response.active_graph.nodes.reserve(node_count);
      for (std::uint32_t index = 0; index < node_count && reader.ok(); ++index) {
        response.active_graph.nodes.push_back({reader.string(), reader.string()});
      }
    }
  }
  response.has_audio_runtime_state = reader.boolean();
  if (reader.ok() && response.has_audio_runtime_state) {
    response.audio_runtime.installed = reader.boolean();
    response.audio_runtime.running = reader.boolean();
    response.audio_runtime.graph_version = reader.scalar<std::uint64_t>();
    response.audio_runtime.configured = reader.boolean();
    response.audio_runtime.configuration.mode =
        reader.enumeration<AudioRuntimeMode>(2);
    response.audio_runtime.configuration.capture_device_id = reader.string();
    response.audio_runtime.configuration.render_device_id = reader.string();
  }
  reader.finish();
  return {std::move(response), reader.error()};
}

}  // namespace sar::control
