#include "core/platform/windows_virtual_wasapi_render_source.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <string_view>
#include <utility>

namespace sar::platform {
namespace {

constexpr std::wstring_view kPrefix = L"Local\\SAR.VirtualWASAPI.v1.";

bool valid_name(std::wstring_view name) noexcept {
  return name.size() > kPrefix.size() && name.size() <= 240 &&
         name.starts_with(kPrefix) &&
         name.find_first_of(L"\\/", kPrefix.size()) == std::wstring_view::npos;
}

}  // namespace

WindowsVirtualWasapiRenderSource::WindowsVirtualWasapiRenderSource(
    void* handle, void* view,
    std::unique_ptr<VirtualWasapiRingAudioSource> source,
    std::uint32_t sample_rate) noexcept
    : handle_(handle), view_(view), source_(std::move(source)),
      sample_rate_(sample_rate) {}

WindowsVirtualWasapiRenderSource::~WindowsVirtualWasapiRenderSource() {
  source_.reset();
  if (view_ != nullptr) UnmapViewOfFile(view_);
  if (handle_ != nullptr) CloseHandle(static_cast<HANDLE>(handle_));
}

WindowsVirtualWasapiRenderSource::OpenResult
WindowsVirtualWasapiRenderSource::open(const std::wstring& object_name) {
  if (!valid_name(object_name)) return {nullptr, "invalid_virtual_wasapi_mapping_name", 0};
  HANDLE handle = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE,
                                    object_name.c_str());
  if (handle == nullptr) {
    return {nullptr, "virtual_wasapi_mapping_open_failed", GetLastError()};
  }
  void* view = MapViewOfFile(handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
  if (view == nullptr) {
    const auto error = GetLastError();
    CloseHandle(handle);
    return {nullptr, "virtual_wasapi_mapping_view_failed", error};
  }
  MEMORY_BASIC_INFORMATION information{};
  if (VirtualQuery(view, &information, sizeof(information)) == 0 ||
      information.RegionSize < sizeof(SarVirtualWasapiTransportHeader)) {
    UnmapViewOfFile(view);
    CloseHandle(handle);
    return {nullptr, "virtual_wasapi_mapping_header_truncated", 0};
  }
  const auto ring = VirtualWasapiTransportRing::attach(
      view, information.RegionSize, SAR_VWASAPI_DIRECTION_RENDER);
  if (!ring || ring->sample_format() != SAR_VWASAPI_SAMPLE_IEEE_FLOAT ||
      ring->bits_per_sample() != 32) {
    UnmapViewOfFile(view);
    CloseHandle(handle);
    return {nullptr, "virtual_wasapi_mapping_invalid_layout", 0};
  }
  const auto* header = static_cast<const SarVirtualWasapiTransportHeader*>(view);
  auto source = std::make_unique<VirtualWasapiRingAudioSource>(*ring);
  return {std::unique_ptr<WindowsVirtualWasapiRenderSource>(
              new WindowsVirtualWasapiRenderSource(handle, view, std::move(source),
                                                   header->sample_rate)),
          {}, 0};
}

}  // namespace sar::platform
