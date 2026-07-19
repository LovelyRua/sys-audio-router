#include "core/platform/windows_virtual_asio_shared_memory.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

std::wstring unique_name(const wchar_t* suffix) {
  return L"Local\\SAR.VirtualASIO.v1.test." + std::to_wstring(GetCurrentProcessId()) +
         L"." + std::to_wstring(GetTickCount64()) + L"." + suffix;
}

sar::platform::VirtualAsioSharedMemoryConfig config() {
  return {
      .format = {48000, 128, 2, 2},
      .queue_capacity_blocks = 8,
  };
}

sar::platform::VirtualAsioSharedMemoryIdentity identity() {
  return {
      .connection_generation = 7,
      .owner_process_id = GetCurrentProcessId(),
      .client_process_id = GetCurrentProcessId(),
      .server_nonce_low = 0x1111111111111111ULL,
      .server_nonce_high = 0x2222222222222222ULL,
      .client_nonce_low = 0x3333333333333333ULL,
      .client_nonce_high = 0x4444444444444444ULL,
  };
}

}  // namespace

int main() {
  using namespace sar::platform;

  assert(!WindowsVirtualAsioSharedMemory::create(L"", config(), identity()).ok());
  assert(!WindowsVirtualAsioSharedMemory::open(L"").ok());
  assert(WindowsVirtualAsioSharedMemory::create(
             L"Global\\outside", config(), identity())
             .errors()
             .front()
             .code == "invalid_virtual_asio_mapping_name");

  const auto name = unique_name(L"roundtrip");
  auto created = WindowsVirtualAsioSharedMemory::create(name, config(), identity());
  assert(created.ok());
  auto owner = created.take_mapping();
  assert(owner->valid());
  assert(owner->owner());
  assert(owner->object_name() == name);
  assert(owner->bytes() == owner->header().total_bytes);
  assert(owner->state() == VirtualAsioSharedMemoryState::Initializing);

  const auto* bytes = static_cast<const std::byte*>(owner->data());
  for (std::size_t index = sizeof(VirtualAsioSharedMemoryHeader);
       index < owner->bytes();
       ++index) {
    assert(bytes[index] == std::byte{0});
  }

  const auto duplicate =
      WindowsVirtualAsioSharedMemory::create(name, config(), identity());
  assert(!duplicate.ok());
  assert(duplicate.errors().front().code ==
         "virtual_asio_mapping_already_exists");

  auto opened = WindowsVirtualAsioSharedMemory::open(name);
  assert(opened.ok());
  auto client = opened.take_mapping();
  assert(client->valid());
  assert(!client->owner());
  assert(client->header().connection_generation == 7);
  assert(client->header().server_nonce_high == 0x2222222222222222ULL);
  assert(client->layout().input_queue.sample_count == 256);

  owner->set_state(VirtualAsioSharedMemoryState::Ready);
  assert(client->state() == VirtualAsioSharedMemoryState::Ready);
  client->set_state(VirtualAsioSharedMemoryState::Stopping);
  assert(owner->state() == VirtualAsioSharedMemoryState::Stopping);

  owner->close();
  assert(!owner->valid());
  assert(client->valid());
  auto held_open = WindowsVirtualAsioSharedMemory::open(name);
  assert(held_open.ok());
  held_open.take_mapping()->close();
  client->close();
  assert(!WindowsVirtualAsioSharedMemory::open(name).ok());

  const auto corrupt_name = unique_name(L"corrupt");
  HANDLE raw = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                  nullptr,
                                  PAGE_READWRITE,
                                  0,
                                  4096,
                                  corrupt_name.c_str());
  assert(raw != nullptr);
  void* raw_view = MapViewOfFile(raw, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
  assert(raw_view != nullptr);
  std::memset(raw_view, 0, 4096);
  auto invalid_open = WindowsVirtualAsioSharedMemory::open(corrupt_name);
  assert(!invalid_open.ok());
  assert(invalid_open.errors().front().code ==
         "invalid_virtual_asio_shared_header");
  UnmapViewOfFile(raw_view);
  CloseHandle(raw);
}
