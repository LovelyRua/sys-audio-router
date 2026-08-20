#include "core/control/session_file_codec.h"

#include "core/control/control_wire_protocol.h"
#include "core/control/preset_file_codec.h"

#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>
#include <utility>

namespace sar::control {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'S', 'A', 'R', 'S'};
constexpr std::size_t kHeaderSize = 12;

SessionFileError file_error(std::string message) {
  return {"invalid_session_file", std::move(message)};
}

template <typename T>
void append_integer(std::vector<std::uint8_t>& bytes, T value) {
  using Unsigned = std::make_unsigned_t<T>;
  auto bits = static_cast<Unsigned>(value);
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    bytes.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
    bits >>= 8U;
  }
}

void append_string(std::vector<std::uint8_t>& bytes,
                   const std::string& value) {
  append_integer(bytes, static_cast<std::uint32_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
}

class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  template <typename T>
  bool integer(T& value) {
    if (remaining() < sizeof(T)) {
      return false;
    }

    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      bits |= static_cast<Unsigned>(bytes_[position_ + index])
              << (index * 8U);
    }
    position_ += sizeof(T);
    value = static_cast<T>(bits);
    return true;
  }

  bool string(std::string& value) {
    std::uint32_t size = 0;
    if (!integer(size) || size > kControlWireMaxStringBytes ||
        remaining() < size) {
      return false;
    }
    const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(position_);
    value.assign(begin, begin + size);
    position_ += size;
    return true;
  }

  bool span(std::size_t size, std::span<const std::uint8_t>& value) {
    if (remaining() < size) {
      return false;
    }
    value = bytes_.subspan(position_, size);
    position_ += size;
    return true;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - position_;
  }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_ = 0;
};

}  // namespace

SessionFileEncodeResult SessionFileEncodeResult::success(
    std::vector<std::uint8_t> bytes) {
  return {std::move(bytes), {}, true};
}

SessionFileEncodeResult SessionFileEncodeResult::failure(
    SessionFileError error) {
  return {{}, std::move(error), false};
}

bool SessionFileEncodeResult::ok() const noexcept { return succeeded_; }

const std::vector<std::uint8_t>& SessionFileEncodeResult::bytes() const noexcept {
  return bytes_;
}

std::vector<std::uint8_t> SessionFileEncodeResult::take_bytes() noexcept {
  return std::move(bytes_);
}

const SessionFileError& SessionFileEncodeResult::error() const noexcept {
  return error_;
}

SessionFileEncodeResult::SessionFileEncodeResult(
    std::vector<std::uint8_t> bytes,
    SessionFileError error,
    bool succeeded) noexcept
    : bytes_(std::move(bytes)),
      error_(std::move(error)),
      succeeded_(succeeded) {}

SessionFileDecodeResult SessionFileDecodeResult::success(
    SessionDocument session) {
  return {std::move(session), {}, true};
}

SessionFileDecodeResult SessionFileDecodeResult::failure(
    SessionFileError error) {
  return {{}, std::move(error), false};
}

bool SessionFileDecodeResult::ok() const noexcept { return succeeded_; }

const SessionDocument& SessionFileDecodeResult::session() const noexcept {
  return session_;
}

SessionDocument SessionFileDecodeResult::take_session() noexcept {
  return std::move(session_);
}

const SessionFileError& SessionFileDecodeResult::error() const noexcept {
  return error_;
}

SessionFileDecodeResult::SessionFileDecodeResult(SessionDocument session,
                                                 SessionFileError error,
                                                 bool succeeded) noexcept
    : session_(std::move(session)),
      error_(std::move(error)),
      succeeded_(succeeded) {}

SessionFileEncodeResult encode_session_file(const SessionDocument& session) {
  const auto validation = validate_session_document(session);
  if (!validation.ok()) {
    return SessionFileEncodeResult::failure({
        "invalid_session",
        validation.errors().empty()
            ? "Session document validation failed."
            : validation.errors().front().message,
    });
  }

  auto encoded_preset = encode_preset_file(session.preset);
  if (!encoded_preset.ok()) {
    return SessionFileEncodeResult::failure({
        "invalid_session",
        encoded_preset.error().message,
    });
  }

  const auto& preset_bytes = encoded_preset.bytes();
  if (preset_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
    return SessionFileEncodeResult::failure(
        file_error("Session preset payload is too large."));
  }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(kHeaderSize + sizeof(std::uint32_t) * 4 +
                preset_bytes.size() + session.audio_runtime.capture_device_id.size() +
                session.audio_runtime.render_device_id.size() + 2);
  bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
  append_integer(bytes, kSessionFileVersion);
  append_integer(bytes, static_cast<std::uint16_t>(0));
  append_integer(bytes, static_cast<std::uint32_t>(0));
  append_integer(bytes, session.schema_version);
  append_integer(bytes, static_cast<std::uint32_t>(preset_bytes.size()));
  bytes.insert(bytes.end(), preset_bytes.begin(), preset_bytes.end());
  append_integer(bytes, static_cast<std::uint8_t>(session.audio_runtime.mode));
  append_string(bytes, session.audio_runtime.capture_device_id);
  append_string(bytes, session.audio_runtime.render_device_id);
  append_integer(bytes, static_cast<std::uint32_t>(
                            session.audio_runtime.endpoints.size()));
  for (const auto& endpoint : session.audio_runtime.endpoints) {
    append_string(bytes, endpoint.endpoint_id);
    append_string(bytes, endpoint.device_id);
    append_integer(bytes, static_cast<std::uint8_t>(endpoint.direction));
    append_integer(bytes,
                   static_cast<std::uint8_t>(endpoint.clock_master ? 1 : 0));
    append_integer(bytes, endpoint.first_channel);
    append_integer(bytes, endpoint.channel_count);
  }
  append_string(bytes, session.audio_runtime.physical_asio_driver_clsid);
  append_integer(bytes, session.audio_runtime.physical_asio_sample_rate);
  append_integer(bytes, session.audio_runtime.physical_asio_block_frames);
  append_integer(bytes, static_cast<std::uint32_t>(
                            session.audio_runtime.physical_asio_input_channels.size()));
  for (const auto channel : session.audio_runtime.physical_asio_input_channels) {
    append_integer(bytes, channel);
  }
  append_integer(bytes, static_cast<std::uint32_t>(
                            session.audio_runtime.physical_asio_output_channels.size()));
  for (const auto channel : session.audio_runtime.physical_asio_output_channels) {
    append_integer(bytes, channel);
  }
  append_integer(bytes, static_cast<std::uint8_t>(session.auto_start ? 1 : 0));
  append_integer(bytes, static_cast<std::uint32_t>(
                            session.virtual_asio_devices.size()));
  for (const auto& device : session.virtual_asio_devices) {
    append_string(bytes, device.device_id);
    append_string(bytes, device.clsid);
    append_string(bytes, device.registry_name);
    append_string(bytes, device.broker_token);
    append_integer(bytes, device.input_channels);
    append_integer(bytes, device.output_channels);
    append_integer(bytes, static_cast<std::uint8_t>(device.enabled ? 1 : 0));
  }

  if (bytes.size() > kSessionFileMaxBytes) {
    return SessionFileEncodeResult::failure(
        file_error("Session file exceeds the maximum supported size."));
  }

  const auto payload_size = static_cast<std::uint32_t>(bytes.size() - kHeaderSize);
  for (std::size_t index = 0; index < sizeof(payload_size); ++index) {
    bytes[8 + index] =
        static_cast<std::uint8_t>((payload_size >> (index * 8U)) & 0xFFU);
  }
  return SessionFileEncodeResult::success(std::move(bytes));
}

SessionFileDecodeResult decode_session_file(
    std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return SessionFileDecodeResult::failure(file_error("Session file is empty."));
  }
  if (bytes.size() > kSessionFileMaxBytes) {
    return SessionFileDecodeResult::failure(
        file_error("Session file exceeds the maximum supported size."));
  }

  Reader reader(bytes);
  std::span<const std::uint8_t> magic;
  std::uint16_t version = 0;
  std::uint16_t reserved = 0;
  std::uint32_t payload_size = 0;
  if (!reader.span(kMagic.size(), magic) ||
      !std::equal(magic.begin(), magic.end(), kMagic.begin()) ||
      !reader.integer(version) || version == 0 ||
      version > kSessionFileVersion ||
      !reader.integer(reserved) || reserved != 0 ||
      !reader.integer(payload_size) || payload_size != reader.remaining()) {
    return SessionFileDecodeResult::failure(
        file_error("Session file header is invalid."));
  }

  SessionDocument session;
  std::uint32_t preset_size = 0;
  std::span<const std::uint8_t> preset_bytes;
  std::uint8_t runtime_mode = 0;
  std::uint8_t auto_start = 0;
  if (!reader.integer(session.schema_version) ||
      session.schema_version == 0 ||
      session.schema_version > kSessionDocumentSchemaVersion ||
      !reader.integer(preset_size) ||
      !reader.span(preset_size, preset_bytes)) {
    return SessionFileDecodeResult::failure(
        file_error("Session document payload is invalid."));
  }

  const auto decoded_preset = decode_preset_file(preset_bytes);
  if (!decoded_preset.ok()) {
    return SessionFileDecodeResult::failure({
        "invalid_preset",
        decoded_preset.error().message,
    });
  }
  session.preset = decoded_preset.preset();

  const auto maximum_runtime_mode = version >= 4
      ? static_cast<std::uint8_t>(AudioRuntimeMode::PhysicalAsio)
      : (version == 1
             ? static_cast<std::uint8_t>(AudioRuntimeMode::WasapiDuplex)
             : static_cast<std::uint8_t>(AudioRuntimeMode::WasapiMatrix));
  if (!reader.integer(runtime_mode) || runtime_mode > maximum_runtime_mode ||
      !reader.string(session.audio_runtime.capture_device_id) ||
      !reader.string(session.audio_runtime.render_device_id)) {
    return SessionFileDecodeResult::failure(
        file_error("Session runtime payload is invalid."));
  }
  if (version >= 2) {
    std::uint32_t endpoint_count = 0;
    if (!reader.integer(endpoint_count) ||
        endpoint_count > kMaximumAudioRuntimeEndpoints) {
      return SessionFileDecodeResult::failure(
          file_error("Session runtime endpoint list is invalid."));
    }
    session.audio_runtime.endpoints.reserve(endpoint_count);
    for (std::uint32_t index = 0; index < endpoint_count; ++index) {
      AudioRuntimeEndpointConfiguration endpoint;
      std::uint8_t direction = 0;
      std::uint8_t clock_master = 0;
      if (!reader.string(endpoint.endpoint_id) ||
          !reader.string(endpoint.device_id) || !reader.integer(direction) ||
          direction > static_cast<std::uint8_t>(
                          AudioRuntimeEndpointDirection::Render) ||
          !reader.integer(clock_master) || clock_master > 1) {
        return SessionFileDecodeResult::failure(
            file_error("Session runtime endpoint is invalid."));
      }
      endpoint.direction =
          static_cast<AudioRuntimeEndpointDirection>(direction);
      endpoint.clock_master = clock_master == 1;
      if (!reader.integer(endpoint.first_channel) ||
          !reader.integer(endpoint.channel_count)) {
        return SessionFileDecodeResult::failure(
            file_error("Session runtime endpoint channel range is invalid."));
      }
      session.audio_runtime.endpoints.push_back(std::move(endpoint));
    }
  }
  if (version >= 4) {
    auto& runtime = session.audio_runtime;
    std::uint32_t input_count = 0;
    std::uint32_t output_count = 0;
    if (!reader.string(runtime.physical_asio_driver_clsid) ||
        !reader.integer(runtime.physical_asio_sample_rate) ||
        !reader.integer(runtime.physical_asio_block_frames) ||
        !reader.integer(input_count) ||
        input_count > kMaximumPhysicalAsioChannels) {
      return SessionFileDecodeResult::failure(
          file_error("Session Physical ASIO configuration is invalid."));
    }
    runtime.physical_asio_input_channels.reserve(input_count);
    for (std::uint32_t index = 0; index < input_count; ++index) {
      std::uint32_t channel = 0;
      if (!reader.integer(channel)) {
        return SessionFileDecodeResult::failure(
            file_error("Session Physical ASIO input channels are invalid."));
      }
      runtime.physical_asio_input_channels.push_back(channel);
    }
    if (!reader.integer(output_count) ||
        output_count > kMaximumPhysicalAsioChannels) {
      return SessionFileDecodeResult::failure(
          file_error("Session Physical ASIO output channels are invalid."));
    }
    runtime.physical_asio_output_channels.reserve(output_count);
    for (std::uint32_t index = 0; index < output_count; ++index) {
      std::uint32_t channel = 0;
      if (!reader.integer(channel)) {
        return SessionFileDecodeResult::failure(
            file_error("Session Physical ASIO output channels are invalid."));
      }
      runtime.physical_asio_output_channels.push_back(channel);
    }
  }
  if (!reader.integer(auto_start) || auto_start > 1) {
    return SessionFileDecodeResult::failure(
        file_error("Session runtime payload is invalid."));
  }
  if (version >= 3) {
    std::uint32_t device_count = 0;
    if (!reader.integer(device_count) || device_count == 0 ||
        device_count > kMaximumVirtualAsioDevices) {
      return SessionFileDecodeResult::failure(
          file_error("Session Virtual ASIO device list is invalid."));
    }
    session.virtual_asio_devices.reserve(device_count);
    for (std::uint32_t index = 0; index < device_count; ++index) {
      VirtualAsioDeviceDefinition device;
      std::uint8_t enabled = 0;
      if (!reader.string(device.device_id) || !reader.string(device.clsid) ||
          !reader.string(device.registry_name) ||
          !reader.string(device.broker_token) ||
          !reader.integer(device.input_channels) ||
          !reader.integer(device.output_channels) ||
          !reader.integer(enabled) || enabled > 1) {
        return SessionFileDecodeResult::failure(
            file_error("Session Virtual ASIO device is invalid."));
      }
      device.enabled = enabled == 1;
      session.virtual_asio_devices.push_back(std::move(device));
    }
  } else {
    session.virtual_asio_devices.push_back(
        default_virtual_asio_device_definition());
  }
  if (reader.remaining() != 0) {
    return SessionFileDecodeResult::failure(
        file_error("Session runtime payload is invalid."));
  }
  session.schema_version = kSessionDocumentSchemaVersion;
  session.audio_runtime.mode = static_cast<AudioRuntimeMode>(runtime_mode);
  session.auto_start = auto_start == 1;

  const auto validation = validate_session_document(session);
  if (!validation.ok()) {
    return SessionFileDecodeResult::failure({
        "invalid_session",
        validation.errors().empty()
            ? "Session document validation failed."
            : validation.errors().front().message,
    });
  }
  return SessionFileDecodeResult::success(std::move(session));
}

}  // namespace sar::control
