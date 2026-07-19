#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sar::platform {

inline constexpr std::size_t kVirtualAsioMaxClientIdBytes = 128;
inline constexpr std::uint32_t kVirtualAsioMaxChannels = 256;
inline constexpr std::uint32_t kVirtualAsioMaxFramesPerBlock = 8192;

struct VirtualAsioFormat {
  std::uint32_t sample_rate = 0;
  std::uint32_t frames_per_block = 0;
  std::uint32_t input_channels = 0;
  std::uint32_t output_channels = 0;

  bool operator==(const VirtualAsioFormat&) const noexcept = default;
};

struct VirtualAsioClientRequest {
  std::string client_id;
  std::uint32_t process_id = 0;
  VirtualAsioFormat format;
};

struct VirtualAsioClientDescriptor {
  std::string client_id;
  std::uint32_t process_id = 0;
  VirtualAsioFormat format;
  std::uint64_t connection_generation = 0;
};

struct VirtualAsioClientError {
  std::string code;
  std::string message;
};

class VirtualAsioClientConnectResult {
 public:
  static VirtualAsioClientConnectResult success(
      VirtualAsioClientDescriptor client);
  static VirtualAsioClientConnectResult failure(
      std::vector<VirtualAsioClientError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const VirtualAsioClientDescriptor& client() const noexcept;
  [[nodiscard]] const std::vector<VirtualAsioClientError>& errors() const noexcept;

 private:
  VirtualAsioClientConnectResult(
      std::optional<VirtualAsioClientDescriptor> client,
      std::vector<VirtualAsioClientError> errors);

  std::optional<VirtualAsioClientDescriptor> client_;
  std::vector<VirtualAsioClientError> errors_;
};

class VirtualAsioClientDisconnectResult {
 public:
  static VirtualAsioClientDisconnectResult success();
  static VirtualAsioClientDisconnectResult failure(
      std::vector<VirtualAsioClientError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<VirtualAsioClientError>& errors() const noexcept;

 private:
  explicit VirtualAsioClientDisconnectResult(
      std::vector<VirtualAsioClientError> errors);

  std::vector<VirtualAsioClientError> errors_;
};

// Control-plane registry for ASIO host connections. Audio callbacks consume the
// resulting fixed format and generation through a separate preallocated transport.
class VirtualAsioClientRegistry {
 public:
  explicit VirtualAsioClientRegistry(std::size_t maximum_clients);

  [[nodiscard]] VirtualAsioClientConnectResult connect(
      VirtualAsioClientRequest request);
  [[nodiscard]] VirtualAsioClientDisconnectResult disconnect(
      const std::string& client_id,
      std::uint64_t connection_generation);

  [[nodiscard]] std::size_t maximum_clients() const noexcept;
  [[nodiscard]] const std::vector<VirtualAsioClientDescriptor>& clients()
      const noexcept;
  [[nodiscard]] const std::optional<VirtualAsioFormat>& active_format()
      const noexcept;

 private:
  std::size_t maximum_clients_;
  std::uint64_t next_generation_ = 1;
  std::optional<VirtualAsioFormat> active_format_;
  std::vector<VirtualAsioClientDescriptor> clients_;
};

[[nodiscard]] std::vector<VirtualAsioClientError>
validate_virtual_asio_client_request(const VirtualAsioClientRequest& request);

}  // namespace sar::platform
