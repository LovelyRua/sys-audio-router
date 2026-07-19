#include "core/control/virtual_asio_broker_protocol.h"

#include <limits>
#include <type_traits>
#include <utility>

namespace sar::control {
namespace {

constexpr std::uint32_t kMagic = 0x42524153U;  // SARB
constexpr std::uint16_t kConnectRequest = 1;
constexpr std::uint16_t kDisconnectRequest = 2;
constexpr std::uint16_t kConnectResponse = 3;
constexpr std::uint16_t kDisconnectResponse = 4;

class Writer {
 public:
  Writer(std::uint16_t type, std::uint64_t request_id) : type_(type) {
    bytes_.resize(kVirtualAsioBrokerHeaderBytes);
    write_header(12, request_id);
  }

  template <typename T>
  void scalar(T value) {
    static_assert(std::is_integral_v<T>);
    using Unsigned = std::make_unsigned_t<T>;
    const auto bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      bytes_.push_back(static_cast<std::uint8_t>(
          (bits >> (index * 8U)) & static_cast<Unsigned>(0xFFU)));
    }
  }

  void boolean(bool value) { scalar<std::uint8_t>(value ? 1U : 0U); }

  void string(const std::string& value) {
    if (value.size() > kVirtualAsioBrokerMaxStringBytes ||
        value.size() > std::numeric_limits<std::uint16_t>::max()) {
      fail(VirtualAsioBrokerErrorCode::StringTooLong);
      return;
    }
    scalar(static_cast<std::uint16_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  void wide_ascii(const std::wstring& value) {
    std::string converted;
    converted.reserve(value.size());
    for (const auto character : value) {
      if (character > 0x7F) {
        fail(VirtualAsioBrokerErrorCode::StringTooLong);
        return;
      }
      converted.push_back(static_cast<char>(character));
    }
    string(converted);
  }

  [[nodiscard]] VirtualAsioBrokerEncodeResult finish() {
    if (error_.code != VirtualAsioBrokerErrorCode::None) {
      return {{}, error_};
    }
    if (bytes_.size() > kVirtualAsioBrokerMaxMessageBytes) {
      return {{}, {VirtualAsioBrokerErrorCode::MessageTooLarge, bytes_.size()}};
    }
    write_header(0, kMagic);
    write_header(4, kVirtualAsioBrokerVersion);
    write_header(6, type_);
    write_header(8, static_cast<std::uint32_t>(
                        bytes_.size() - kVirtualAsioBrokerHeaderBytes));
    return {std::move(bytes_), {}};
  }

 private:
  template <typename T>
  void write_header(std::size_t offset, T value) {
    using Unsigned = std::make_unsigned_t<T>;
    const auto bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      bytes_[offset + index] = static_cast<std::uint8_t>(
          (bits >> (index * 8U)) & static_cast<Unsigned>(0xFFU));
    }
  }

  void fail(VirtualAsioBrokerErrorCode code) {
    if (error_.code == VirtualAsioBrokerErrorCode::None) {
      error_ = {code, bytes_.size()};
    }
  }

  std::uint16_t type_ = 0;
  std::vector<std::uint8_t> bytes_;
  VirtualAsioBrokerWireError error_;
};

class Reader {
 public:
  Reader(std::span<const std::uint8_t> bytes, std::uint16_t expected_type)
      : bytes_(bytes) {
    if (bytes.size() > kVirtualAsioBrokerMaxMessageBytes) {
      fail(VirtualAsioBrokerErrorCode::MessageTooLarge);
      return;
    }
    if (bytes.size() < kVirtualAsioBrokerHeaderBytes) {
      fail(VirtualAsioBrokerErrorCode::Truncated);
      return;
    }
    const auto magic = scalar<std::uint32_t>();
    const auto version = scalar<std::uint16_t>();
    const auto type = scalar<std::uint16_t>();
    const auto payload_bytes = scalar<std::uint32_t>();
    request_id_ = scalar<std::uint64_t>();
    if (!ok()) {
      return;
    }
    if (magic != kMagic) {
      fail(VirtualAsioBrokerErrorCode::InvalidMagic);
    } else if (version != kVirtualAsioBrokerVersion) {
      fail(VirtualAsioBrokerErrorCode::UnsupportedVersion);
    } else if (type != expected_type) {
      fail(VirtualAsioBrokerErrorCode::UnexpectedMessageType);
    } else if (payload_bytes != bytes.size() - kVirtualAsioBrokerHeaderBytes) {
      fail(VirtualAsioBrokerErrorCode::InvalidLength);
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
      value |= static_cast<Unsigned>(bytes_[offset_ + index]) <<
               (index * 8U);
    }
    offset_ += sizeof(T);
    return static_cast<T>(value);
  }

  bool boolean() {
    const auto value = scalar<std::uint8_t>();
    if (ok() && value > 1) {
      fail(VirtualAsioBrokerErrorCode::InvalidBoolean);
    }
    return value == 1;
  }

  std::string string() {
    const auto length = scalar<std::uint16_t>();
    if (!ok()) {
      return {};
    }
    if (length > kVirtualAsioBrokerMaxStringBytes) {
      fail(VirtualAsioBrokerErrorCode::StringTooLong);
      return {};
    }
    if (!require(length)) {
      return {};
    }
    std::string result(
        reinterpret_cast<const char*>(bytes_.data() + offset_), length);
    offset_ += length;
    return result;
  }

  std::wstring wide_ascii() {
    const auto value = string();
    return {value.begin(), value.end()};
  }

  void finish() {
    if (ok() && offset_ != bytes_.size()) {
      fail(VirtualAsioBrokerErrorCode::TrailingBytes);
    }
  }

  [[nodiscard]] bool ok() const noexcept {
    return error_.code == VirtualAsioBrokerErrorCode::None;
  }
  [[nodiscard]] std::uint64_t request_id() const noexcept {
    return request_id_;
  }
  [[nodiscard]] VirtualAsioBrokerWireError error() const noexcept {
    return error_;
  }

 private:
  bool require(std::size_t bytes) {
    if (bytes > bytes_.size() - offset_) {
      fail(VirtualAsioBrokerErrorCode::Truncated);
      return false;
    }
    return true;
  }

  void fail(VirtualAsioBrokerErrorCode code) {
    if (error_.code == VirtualAsioBrokerErrorCode::None) {
      error_ = {code, offset_};
    }
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t offset_ = 0;
  std::uint64_t request_id_ = 0;
  VirtualAsioBrokerWireError error_;
};

void write_format(Writer& writer, const platform::VirtualAsioFormat& format) {
  writer.scalar(format.sample_rate);
  writer.scalar(format.frames_per_block);
  writer.scalar(format.input_channels);
  writer.scalar(format.output_channels);
}

platform::VirtualAsioFormat read_format(Reader& reader) {
  return {reader.scalar<std::uint32_t>(), reader.scalar<std::uint32_t>(),
          reader.scalar<std::uint32_t>(), reader.scalar<std::uint32_t>()};
}

void write_error(Writer& writer,
                 bool accepted,
                 const std::string& code,
                 const std::string& message) {
  writer.boolean(accepted);
  writer.string(code);
  writer.string(message);
}

}  // namespace

VirtualAsioBrokerEncodeResult encode_virtual_asio_broker_connect(
    const VirtualAsioBrokerConnectRequest& request) {
  Writer writer(kConnectRequest, request.request_id);
  writer.string(request.client_id);
  write_format(writer, request.format);
  writer.scalar(request.queue_capacity_blocks);
  writer.scalar(request.client_nonce_low);
  writer.scalar(request.client_nonce_high);
  return writer.finish();
}

VirtualAsioBrokerDecodeResult<VirtualAsioBrokerConnectRequest>
decode_virtual_asio_broker_connect(std::span<const std::uint8_t> bytes) {
  Reader reader(bytes, kConnectRequest);
  VirtualAsioBrokerConnectRequest value;
  value.request_id = reader.request_id();
  value.client_id = reader.string();
  value.format = read_format(reader);
  value.queue_capacity_blocks = reader.scalar<std::uint32_t>();
  value.client_nonce_low = reader.scalar<std::uint64_t>();
  value.client_nonce_high = reader.scalar<std::uint64_t>();
  reader.finish();
  return {std::move(value), reader.error()};
}

VirtualAsioBrokerEncodeResult encode_virtual_asio_broker_disconnect(
    const VirtualAsioBrokerDisconnectRequest& request) {
  Writer writer(kDisconnectRequest, request.request_id);
  writer.string(request.client_id);
  writer.scalar(request.connection_generation);
  return writer.finish();
}

VirtualAsioBrokerDecodeResult<VirtualAsioBrokerDisconnectRequest>
decode_virtual_asio_broker_disconnect(std::span<const std::uint8_t> bytes) {
  Reader reader(bytes, kDisconnectRequest);
  VirtualAsioBrokerDisconnectRequest value;
  value.request_id = reader.request_id();
  value.client_id = reader.string();
  value.connection_generation = reader.scalar<std::uint64_t>();
  reader.finish();
  return {std::move(value), reader.error()};
}

VirtualAsioBrokerEncodeResult encode_virtual_asio_broker_connect_response(
    const VirtualAsioBrokerConnectResponse& response) {
  Writer writer(kConnectResponse, response.request_id);
  write_error(writer, response.accepted, response.error_code,
              response.error_message);
  writer.scalar(response.connection_generation);
  writer.wide_ascii(response.names.mapping);
  writer.wide_ascii(response.names.input_event);
  writer.wide_ascii(response.names.output_event);
  writer.wide_ascii(response.names.shutdown_event);
  writer.scalar(response.server_nonce_low);
  writer.scalar(response.server_nonce_high);
  return writer.finish();
}

VirtualAsioBrokerDecodeResult<VirtualAsioBrokerConnectResponse>
decode_virtual_asio_broker_connect_response(
    std::span<const std::uint8_t> bytes) {
  Reader reader(bytes, kConnectResponse);
  VirtualAsioBrokerConnectResponse value;
  value.request_id = reader.request_id();
  value.accepted = reader.boolean();
  value.error_code = reader.string();
  value.error_message = reader.string();
  value.connection_generation = reader.scalar<std::uint64_t>();
  value.names.mapping = reader.wide_ascii();
  value.names.input_event = reader.wide_ascii();
  value.names.output_event = reader.wide_ascii();
  value.names.shutdown_event = reader.wide_ascii();
  value.server_nonce_low = reader.scalar<std::uint64_t>();
  value.server_nonce_high = reader.scalar<std::uint64_t>();
  reader.finish();
  return {std::move(value), reader.error()};
}

VirtualAsioBrokerEncodeResult encode_virtual_asio_broker_disconnect_response(
    const VirtualAsioBrokerDisconnectResponse& response) {
  Writer writer(kDisconnectResponse, response.request_id);
  write_error(writer, response.accepted, response.error_code,
              response.error_message);
  return writer.finish();
}

VirtualAsioBrokerDecodeResult<VirtualAsioBrokerDisconnectResponse>
decode_virtual_asio_broker_disconnect_response(
    std::span<const std::uint8_t> bytes) {
  Reader reader(bytes, kDisconnectResponse);
  VirtualAsioBrokerDisconnectResponse value;
  value.request_id = reader.request_id();
  value.accepted = reader.boolean();
  value.error_code = reader.string();
  value.error_message = reader.string();
  reader.finish();
  return {std::move(value), reader.error()};
}

}  // namespace sar::control
