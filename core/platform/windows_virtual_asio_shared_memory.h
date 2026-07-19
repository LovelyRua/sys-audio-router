#pragma once

#include "core/platform/virtual_asio_shared_memory_layout.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sar::platform {

struct WindowsVirtualAsioSharedMemoryError {
  std::string code;
  std::string message;
  std::uint32_t native_error = 0;
};

class WindowsVirtualAsioSharedMemory;

class WindowsVirtualAsioSharedMemoryOpenResult {
 public:
  static WindowsVirtualAsioSharedMemoryOpenResult success(
      std::unique_ptr<WindowsVirtualAsioSharedMemory> mapping);
  static WindowsVirtualAsioSharedMemoryOpenResult failure(
      std::vector<WindowsVirtualAsioSharedMemoryError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsVirtualAsioSharedMemory& mapping() noexcept;
  [[nodiscard]] const WindowsVirtualAsioSharedMemory& mapping() const noexcept;
  [[nodiscard]] std::unique_ptr<WindowsVirtualAsioSharedMemory> take_mapping()
      noexcept;
  [[nodiscard]] const std::vector<WindowsVirtualAsioSharedMemoryError>& errors()
      const noexcept;

 private:
  WindowsVirtualAsioSharedMemoryOpenResult(
      std::unique_ptr<WindowsVirtualAsioSharedMemory> mapping,
      std::vector<WindowsVirtualAsioSharedMemoryError> errors);

  std::unique_ptr<WindowsVirtualAsioSharedMemory> mapping_;
  std::vector<WindowsVirtualAsioSharedMemoryError> errors_;
};

class WindowsVirtualAsioSharedMemory {
 public:
  WindowsVirtualAsioSharedMemory(const WindowsVirtualAsioSharedMemory&) = delete;
  WindowsVirtualAsioSharedMemory& operator=(
      const WindowsVirtualAsioSharedMemory&) = delete;
  WindowsVirtualAsioSharedMemory(WindowsVirtualAsioSharedMemory&& other) noexcept;
  WindowsVirtualAsioSharedMemory& operator=(
      WindowsVirtualAsioSharedMemory&& other) noexcept;
  ~WindowsVirtualAsioSharedMemory();

  [[nodiscard]] static WindowsVirtualAsioSharedMemoryOpenResult create(
      std::wstring object_name,
      const VirtualAsioSharedMemoryConfig& config,
      const VirtualAsioSharedMemoryIdentity& identity);
  [[nodiscard]] static WindowsVirtualAsioSharedMemoryOpenResult open(
      std::wstring object_name);

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool owner() const noexcept;
  [[nodiscard]] const std::wstring& object_name() const noexcept;
  [[nodiscard]] std::uint64_t bytes() const noexcept;
  [[nodiscard]] const VirtualAsioSharedMemoryLayout& layout() const noexcept;
  [[nodiscard]] const VirtualAsioSharedMemoryHeader& header() const noexcept;
  [[nodiscard]] void* data() noexcept;
  [[nodiscard]] const void* data() const noexcept;
  [[nodiscard]] VirtualAsioSharedMemoryState state() const noexcept;

  void set_state(VirtualAsioSharedMemoryState state) noexcept;
  void close() noexcept;

 private:
  WindowsVirtualAsioSharedMemory(std::wstring object_name,
                                 void* mapping_handle,
                                 void* view,
                                 std::uint64_t bytes,
                                 VirtualAsioSharedMemoryLayout layout,
                                 bool owner) noexcept;

  std::wstring object_name_;
  void* mapping_handle_ = nullptr;
  void* view_ = nullptr;
  std::uint64_t bytes_ = 0;
  VirtualAsioSharedMemoryLayout layout_;
  bool owner_ = false;
};

}  // namespace sar::platform
