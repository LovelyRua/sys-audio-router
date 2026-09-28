#include "core/platform/windows_virtual_wasapi_render_source.h"
#include "core/platform/virtual_wasapi_transport_layout.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>

int main() {
  using namespace sar::platform;
  assert(!WindowsVirtualWasapiRenderSource::open(L"bad-name").ok());
  const auto name = L"Local\\SAR.VirtualWASAPI.v1.smoke-" +
                    std::to_wstring(GetCurrentProcessId());
  VirtualWasapiTransportConfig config;
  config.frames_per_slot = 4;
  config.slot_count = 2;
  const auto layout = calculate_virtual_wasapi_transport_layout(config);
  assert(layout.ok());
  HANDLE handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, layout.header.total_size, name.c_str());
  assert(handle != nullptr && GetLastError() != ERROR_ALREADY_EXISTS);
  void* view = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  assert(view != nullptr);
  assert(!WindowsVirtualWasapiRenderSource::open(name).ok());
  assert(VirtualWasapiTransportRing::initialize(view, layout.header.total_size,
                                                layout.header));
  auto producer = VirtualWasapiTransportRing::attach(
      view, layout.header.total_size, SAR_VWASAPI_DIRECTION_RENDER);
  assert(producer);
  auto opened = WindowsVirtualWasapiRenderSource::open(name);
  assert(opened.ok());
  assert(opened.mapping->sample_rate() == config.sample_rate);
  assert(opened.mapping->channels() == config.channel_count);
  const std::array<float, 4> samples{0.1F, 0.2F, 0.3F, 0.4F};
  std::array<std::byte, sizeof(samples)> bytes{};
  std::memcpy(bytes.data(), samples.data(), bytes.size());
  assert(producer->push(bytes, {2, 0, 0, 0}) == VirtualWasapiPacketStatus::Completed);
  sar::realtime::AudioBuffer output(2, 2);
  assert(opened.mapping->source().available_frames() == 2);
  assert(opened.mapping->source().read(output));
  assert(output.channel(0)[0] == 0.1F && output.channel(1)[1] == 0.4F);
  opened.mapping.reset();
  UnmapViewOfFile(view);
  CloseHandle(handle);
}
