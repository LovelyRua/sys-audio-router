#include "core/platform/windows_virtual_asio_shared_memory.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstring>
#include <string_view>
#include <utility>

namespace sar::platform {
namespace {

constexpr std::wstring_view kMappingPrefix = L"Local\\SAR.VirtualASIO.v1.";
constexpr std::size_t kMaximumMappingNameCharacters = 240;

WindowsVirtualAsioSharedMemoryOpenResult native_failure(
    std::string code,
    std::string message,
    DWORD native_error) {
  return WindowsVirtualAsioSharedMemoryOpenResult::failure({
      {std::move(code), std::move(message), native_error},
  });
}

bool valid_state(VirtualAsioSharedMemoryState state) noexcept {
  return state == VirtualAsioSharedMemoryState::Initializing ||
         state == VirtualAsioSharedMemoryState::Ready ||
         state == VirtualAsioSharedMemoryState::Stopping ||
         state == VirtualAsioSharedMemoryState::Faulted;
}

bool valid_mapping_name(std::wstring_view name) noexcept {
  return name.size() <= kMaximumMappingNameCharacters &&
         name.starts_with(kMappingPrefix) &&
         name.find(L'\\', kMappingPrefix.size()) == std::wstring_view::npos &&
         name.find(L'/') == std::wstring_view::npos;
}

std::uint64_t mapped_region_bytes(void* view) noexcept {
  MEMORY_BASIC_INFORMATION information{};
  if (VirtualQuery(view, &information, sizeof(information)) == 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(information.RegionSize);
}

}  // namespace

WindowsVirtualAsioSharedMemoryOpenResult
WindowsVirtualAsioSharedMemoryOpenResult::success(
    std::unique_ptr<WindowsVirtualAsioSharedMemory> mapping) {
  return {std::move(mapping), {}};
}

WindowsVirtualAsioSharedMemoryOpenResult
WindowsVirtualAsioSharedMemoryOpenResult::failure(
    std::vector<WindowsVirtualAsioSharedMemoryError> errors) {
  return {nullptr, std::move(errors)};
}

bool WindowsVirtualAsioSharedMemoryOpenResult::ok() const noexcept {
  return mapping_ != nullptr && errors_.empty();
}

WindowsVirtualAsioSharedMemory&
WindowsVirtualAsioSharedMemoryOpenResult::mapping() noexcept {
  return *mapping_;
}

const WindowsVirtualAsioSharedMemory&
WindowsVirtualAsioSharedMemoryOpenResult::mapping() const noexcept {
  return *mapping_;
}

std::unique_ptr<WindowsVirtualAsioSharedMemory>
WindowsVirtualAsioSharedMemoryOpenResult::take_mapping() noexcept {
  return std::move(mapping_);
}

const std::vector<WindowsVirtualAsioSharedMemoryError>&
WindowsVirtualAsioSharedMemoryOpenResult::errors() const noexcept {
  return errors_;
}

WindowsVirtualAsioSharedMemoryOpenResult::
    WindowsVirtualAsioSharedMemoryOpenResult(
        std::unique_ptr<WindowsVirtualAsioSharedMemory> mapping,
        std::vector<WindowsVirtualAsioSharedMemoryError> errors)
    : mapping_(std::move(mapping)), errors_(std::move(errors)) {}

WindowsVirtualAsioSharedMemory::WindowsVirtualAsioSharedMemory(
    WindowsVirtualAsioSharedMemory&& other) noexcept
    : object_name_(std::move(other.object_name_)),
      mapping_handle_(std::exchange(other.mapping_handle_, nullptr)),
      view_(std::exchange(other.view_, nullptr)),
      bytes_(std::exchange(other.bytes_, 0)),
      layout_(std::move(other.layout_)),
      owner_(std::exchange(other.owner_, false)) {}

WindowsVirtualAsioSharedMemory& WindowsVirtualAsioSharedMemory::operator=(
    WindowsVirtualAsioSharedMemory&& other) noexcept {
  if (this != &other) {
    close();
    object_name_ = std::move(other.object_name_);
    mapping_handle_ = std::exchange(other.mapping_handle_, nullptr);
    view_ = std::exchange(other.view_, nullptr);
    bytes_ = std::exchange(other.bytes_, 0);
    layout_ = std::move(other.layout_);
    owner_ = std::exchange(other.owner_, false);
  }
  return *this;
}

WindowsVirtualAsioSharedMemory::~WindowsVirtualAsioSharedMemory() {
  close();
}

WindowsVirtualAsioSharedMemoryOpenResult
WindowsVirtualAsioSharedMemory::create(
    std::wstring object_name,
    const VirtualAsioSharedMemoryConfig& config,
    const VirtualAsioSharedMemoryIdentity& identity) {
  if (object_name.empty()) {
    return WindowsVirtualAsioSharedMemoryOpenResult::failure({
        {"empty_virtual_asio_mapping_name",
         "Virtual ASIO mapping object name must not be empty.", 0},
    });
  }
  if (!valid_mapping_name(object_name)) {
    return WindowsVirtualAsioSharedMemoryOpenResult::failure({
        {"invalid_virtual_asio_mapping_name",
         "Virtual ASIO mappings require a bounded Local namespace name.", 0},
    });
  }
  auto calculated = calculate_virtual_asio_shared_memory_layout(config, identity);
  if (!calculated.ok()) {
    return WindowsVirtualAsioSharedMemoryOpenResult::failure({
        {calculated.error().code, calculated.error().message, 0},
    });
  }
  const auto bytes = calculated.layout().header.total_bytes;
  const DWORD high = static_cast<DWORD>(bytes >> 32U);
  const DWORD low = static_cast<DWORD>(bytes & 0xFFFFFFFFULL);
  HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                      nullptr,
                                      PAGE_READWRITE,
                                      high,
                                      low,
                                      object_name.c_str());
  if (mapping == nullptr) {
    const auto error = GetLastError();
    return native_failure("virtual_asio_mapping_create_failed",
                          "Could not create Virtual ASIO shared memory mapping.",
                          error);
  }
  const auto create_error = GetLastError();
  if (create_error == ERROR_ALREADY_EXISTS) {
    CloseHandle(mapping);
    return native_failure("virtual_asio_mapping_already_exists",
                          "Virtual ASIO shared memory name is already in use.",
                          create_error);
  }

  void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                             static_cast<SIZE_T>(bytes));
  if (view == nullptr) {
    const auto error = GetLastError();
    CloseHandle(mapping);
    return native_failure("virtual_asio_mapping_view_failed",
                          "Could not map the new Virtual ASIO shared memory.",
                          error);
  }
  std::memset(view, 0, static_cast<std::size_t>(bytes));
  std::memcpy(view,
              &calculated.layout().header,
              sizeof(VirtualAsioSharedMemoryHeader));
  return WindowsVirtualAsioSharedMemoryOpenResult::success(
      std::unique_ptr<WindowsVirtualAsioSharedMemory>(
          new WindowsVirtualAsioSharedMemory(std::move(object_name),
                                             mapping,
                                             view,
                                             bytes,
                                             calculated.layout(),
                                             true)));
}

WindowsVirtualAsioSharedMemoryOpenResult WindowsVirtualAsioSharedMemory::open(
    std::wstring object_name) {
  if (object_name.empty()) {
    return WindowsVirtualAsioSharedMemoryOpenResult::failure({
        {"empty_virtual_asio_mapping_name",
         "Virtual ASIO mapping object name must not be empty.", 0},
    });
  }
  if (!valid_mapping_name(object_name)) {
    return WindowsVirtualAsioSharedMemoryOpenResult::failure({
        {"invalid_virtual_asio_mapping_name",
         "Virtual ASIO mappings require a bounded Local namespace name.", 0},
    });
  }
  HANDLE mapping =
      OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, object_name.c_str());
  if (mapping == nullptr) {
    const auto error = GetLastError();
    return native_failure("virtual_asio_mapping_open_failed",
                          "Could not open Virtual ASIO shared memory mapping.",
                          error);
  }
  void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
  if (view == nullptr) {
    const auto error = GetLastError();
    CloseHandle(mapping);
    return native_failure("virtual_asio_mapping_view_failed",
                          "Could not map existing Virtual ASIO shared memory.",
                          error);
  }

  const auto region_bytes = mapped_region_bytes(view);
  if (region_bytes < sizeof(VirtualAsioSharedMemoryHeader)) {
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return WindowsVirtualAsioSharedMemoryOpenResult::failure({
        {"virtual_asio_mapping_header_truncated",
         "Virtual ASIO mapping is smaller than its fixed header.", 0},
    });
  }
  VirtualAsioSharedMemoryHeader header;
  std::memcpy(&header, view, sizeof(header));
  auto validated =
      validate_virtual_asio_shared_memory_header(header, region_bytes);
  if (!validated.ok()) {
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return WindowsVirtualAsioSharedMemoryOpenResult::failure({
        {validated.error().code, validated.error().message, 0},
    });
  }
  return WindowsVirtualAsioSharedMemoryOpenResult::success(
      std::unique_ptr<WindowsVirtualAsioSharedMemory>(
          new WindowsVirtualAsioSharedMemory(std::move(object_name),
                                             mapping,
                                             view,
                                             validated.layout().header.total_bytes,
                                             validated.layout(),
                                             false)));
}

bool WindowsVirtualAsioSharedMemory::valid() const noexcept {
  return mapping_handle_ != nullptr && view_ != nullptr;
}

bool WindowsVirtualAsioSharedMemory::owner() const noexcept {
  return owner_;
}

const std::wstring& WindowsVirtualAsioSharedMemory::object_name() const noexcept {
  return object_name_;
}

std::uint64_t WindowsVirtualAsioSharedMemory::bytes() const noexcept {
  return bytes_;
}

const VirtualAsioSharedMemoryLayout&
WindowsVirtualAsioSharedMemory::layout() const noexcept {
  return layout_;
}

const VirtualAsioSharedMemoryHeader&
WindowsVirtualAsioSharedMemory::header() const noexcept {
  return *static_cast<const VirtualAsioSharedMemoryHeader*>(view_);
}

void* WindowsVirtualAsioSharedMemory::data() noexcept {
  return view_;
}

const void* WindowsVirtualAsioSharedMemory::data() const noexcept {
  return view_;
}

VirtualAsioSharedMemoryState WindowsVirtualAsioSharedMemory::state()
    const noexcept {
  if (!valid()) {
    return VirtualAsioSharedMemoryState::Faulted;
  }
  auto* state = reinterpret_cast<volatile LONG*>(
      const_cast<std::uint32_t*>(&header().state));
  const auto value = InterlockedCompareExchange(state, 0, 0);
  return static_cast<VirtualAsioSharedMemoryState>(value);
}

void WindowsVirtualAsioSharedMemory::set_state(
    VirtualAsioSharedMemoryState state_value) noexcept {
  if (!valid() || !valid_state(state_value)) {
    return;
  }
  auto* state = reinterpret_cast<volatile LONG*>(
      static_cast<std::byte*>(view_) +
      offsetof(VirtualAsioSharedMemoryHeader, state));
  InterlockedExchange(state, static_cast<LONG>(state_value));
}

void WindowsVirtualAsioSharedMemory::close() noexcept {
  if (view_ != nullptr) {
    UnmapViewOfFile(view_);
    view_ = nullptr;
  }
  if (mapping_handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(mapping_handle_));
    mapping_handle_ = nullptr;
  }
  bytes_ = 0;
  owner_ = false;
}

WindowsVirtualAsioSharedMemory::WindowsVirtualAsioSharedMemory(
    std::wstring object_name,
    void* mapping_handle,
    void* view,
    std::uint64_t bytes,
    VirtualAsioSharedMemoryLayout layout,
    bool owner) noexcept
    : object_name_(std::move(object_name)),
      mapping_handle_(mapping_handle),
      view_(view),
      bytes_(bytes),
      layout_(std::move(layout)),
      owner_(owner) {}

}  // namespace sar::platform
