#include "core/platform/virtual_wasapi_transport_layout.h"
#include "core/platform/virtual_wasapi_transport_ring.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

std::atomic_bool stop_requested = false;

BOOL WINAPI on_console_event(DWORD event) {
  if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
      event == CTRL_CLOSE_EVENT || event == CTRL_SHUTDOWN_EVENT) {
    stop_requested.store(true, std::memory_order_relaxed);
    return TRUE;
  }
  return FALSE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::wstring name = L"Local\\SAR.VirtualWASAPI.v1.lab-tone";
  unsigned long seconds = 0;
  for (int index = 1; index < argc; ++index) {
    if (std::wcscmp(argv[index], L"--name") == 0 && index + 1 < argc) {
      name = argv[++index];
    } else if (std::wcscmp(argv[index], L"--seconds") == 0 &&
               index + 1 < argc) {
      wchar_t* end = nullptr;
      seconds = std::wcstoul(argv[++index], &end, 10);
      if (*end != L'\0' || seconds == 0 || seconds > 3600) {
        std::wcerr << L"--seconds must be between 1 and 3600.\n";
        return 2;
      }
    } else {
      std::wcerr << L"Usage: sar_virtual_wasapi_lab_producer "
                    L"[--name Local\\SAR.VirtualWASAPI.v1.TOKEN] "
                    L"[--seconds 1..3600]\n";
      return 2;
    }
  }
  constexpr std::wstring_view kPrefix = L"Local\\SAR.VirtualWASAPI.v1.";
  if (!name.starts_with(kPrefix) || name.size() <= kPrefix.size() ||
      name.size() > 240 ||
      name.find_first_of(L"\\/", kPrefix.size()) != std::wstring::npos) {
    std::wcerr << L"Invalid mapping name.\n";
    return 2;
  }

  sar::platform::VirtualWasapiTransportConfig config;
  config.frames_per_slot = 128;
  config.slot_count = 16;
  const auto layout = sar::platform::calculate_virtual_wasapi_transport_layout(config);
  if (!layout.ok()) return 1;
  HANDLE handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                     PAGE_READWRITE, 0,
                                     layout.header.total_size, name.c_str());
  if (handle == nullptr) {
    std::wcerr << L"CreateFileMapping failed: " << GetLastError() << L'\n';
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    std::wcerr << L"Mapping name is already in use.\n";
    CloseHandle(handle);
    return 1;
  }
  void* view = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (view == nullptr ||
      !sar::platform::VirtualWasapiTransportRing::initialize(
          view, layout.header.total_size, layout.header)) {
    std::wcerr << L"Could not initialize shared mapping.\n";
    if (view != nullptr) UnmapViewOfFile(view);
    CloseHandle(handle);
    return 1;
  }
  auto ring = sar::platform::VirtualWasapiTransportRing::attach(
      view, layout.header.total_size, SAR_VWASAPI_DIRECTION_RENDER);
  if (!ring) {
    UnmapViewOfFile(view);
    CloseHandle(handle);
    return 1;
  }
  SetConsoleCtrlHandler(on_console_event, TRUE);
  std::wcout << L"Virtual WASAPI lab tone ready: " << name << L'\n';
  constexpr double kPi = 3.14159265358979323846;
  std::array<float, 256> samples{};
  std::array<std::byte, sizeof(samples)> bytes{};
  std::uint64_t position = 0;
  const auto start = std::chrono::steady_clock::now();
  while (!stop_requested.load(std::memory_order_relaxed) &&
         (seconds == 0 || position < static_cast<std::uint64_t>(seconds) * 48000)) {
    for (std::size_t frame = 0; frame < config.frames_per_slot; ++frame) {
      const auto phase = 2.0 * kPi * 440.0 *
                         static_cast<double>(position + frame) / config.sample_rate;
      const auto sample = static_cast<float>(0.1 * std::sin(phase));
      samples[frame * 2] = sample;
      samples[frame * 2 + 1] = sample;
    }
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    static_cast<void>(ring->push(bytes, {config.frames_per_slot, 0, position, 0}));
    position += config.frames_per_slot;
    const auto elapsed = std::chrono::duration<double>(
        static_cast<double>(position) / config.sample_rate);
    std::this_thread::sleep_until(
        start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(elapsed));
  }
  SetConsoleCtrlHandler(on_console_event, FALSE);
  const auto counters = ring->counters();
  std::wcout << L"produced_frames=" << counters.produced_frames
             << L" dropped_frames=" << counters.dropped_frames << L'\n';
  UnmapViewOfFile(view);
  CloseHandle(handle);
  return 0;
}
