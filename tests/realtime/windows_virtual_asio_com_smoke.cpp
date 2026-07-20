#include "driver/windows_virtual_asio_com.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Unknwn.h>

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <array>
#include <cassert>
#include <cstring>
#include <string>

namespace {

using DllCanUnloadNowFunction = HRESULT(STDAPICALLTYPE*)();
using DllGetClassObjectFunction = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID,
                                                          void**);

void buffer_switch(long, ASIOBool) {}
void sample_rate_changed(ASIOSampleRate) {}
long asio_message(long, long, void*, double*) { return 0; }
ASIOTime* buffer_switch_time_info(ASIOTime* time, long, ASIOBool) {
  return time;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  HMODULE module = LoadLibraryW(argv[1]);
  assert(module != nullptr);
  const auto can_unload = reinterpret_cast<DllCanUnloadNowFunction>(
      GetProcAddress(module, "DllCanUnloadNow"));
  const auto get_class = reinterpret_cast<DllGetClassObjectFunction>(
      GetProcAddress(module, "DllGetClassObject"));
  assert(can_unload != nullptr);
  assert(get_class != nullptr);
  assert(can_unload() == S_OK);

  CLSID unknown = sar::driver::kWindowsVirtualAsioClsid;
  ++unknown.Data1;
  void* object = reinterpret_cast<void*>(1);
  assert(get_class(unknown, IID_IClassFactory, &object) ==
         CLASS_E_CLASSNOTAVAILABLE);
  assert(object == nullptr);
  assert(get_class(sar::driver::kWindowsVirtualAsioClsid,
                   IID_IClassFactory, nullptr) == E_POINTER);

  IClassFactory* factory = nullptr;
  assert(get_class(sar::driver::kWindowsVirtualAsioClsid,
                   IID_IClassFactory,
                   reinterpret_cast<void**>(&factory)) == S_OK);
  assert(factory != nullptr);
  assert(can_unload() == S_FALSE);
  assert(factory->CreateInstance(reinterpret_cast<IUnknown*>(1), IID_IUnknown,
                                 &object) == CLASS_E_NOAGGREGATION);
  assert(object == nullptr);

  IUnknown* driver = nullptr;
  assert(factory->CreateInstance(nullptr, IID_IUnknown,
                                 reinterpret_cast<void**>(&driver)) == S_OK);
  assert(driver != nullptr);
  assert(can_unload() == S_FALSE);
  auto* asio = static_cast<IASIO*>(driver);
  char driver_name[32] = {};
  asio->getDriverName(driver_name);
  assert(std::strcmp(driver_name, "System Audio Route") == 0);
  assert(asio->getDriverVersion() == 1);
  assert(asio->start() == ASE_InvalidMode);
  assert(asio->init(nullptr) == ASIOTrue);

  long input_channels = 0;
  long output_channels = 0;
  assert(asio->getChannels(&input_channels, &output_channels) == ASE_OK);
  assert(input_channels == 2);
  assert(output_channels == 2);
  long minimum = 0;
  long maximum = 0;
  long preferred = 0;
  long granularity = 0;
  assert(asio->getBufferSize(&minimum, &maximum, &preferred, &granularity) ==
         ASE_OK);
  assert(minimum == 64);
  assert(maximum == 2048);
  assert(preferred == 256);
  assert(granularity == -1);
  assert(asio->canSampleRate(48000.0) == ASE_OK);
  assert(asio->canSampleRate(12345.0) == ASE_NoClock);
  assert(asio->setSampleRate(96000.0) == ASE_OK);
  ASIOSampleRate sample_rate = 0;
  assert(asio->getSampleRate(&sample_rate) == ASE_OK);
  assert(sample_rate == 96000.0);

  ASIOChannelInfo channel_info{};
  channel_info.channel = 1;
  channel_info.isInput = ASIOFalse;
  assert(asio->getChannelInfo(&channel_info) == ASE_OK);
  assert(channel_info.isActive == ASIOFalse);
  assert(channel_info.type == ASIOSTFloat32LSB);
  assert(std::strcmp(channel_info.name, "Output 2") == 0);

  std::array<ASIOBufferInfo, 4> buffer_infos{};
  for (long index = 0; index < 2; ++index) {
    buffer_infos[static_cast<std::size_t>(index)].isInput = ASIOTrue;
    buffer_infos[static_cast<std::size_t>(index)].channelNum = index;
    buffer_infos[static_cast<std::size_t>(index + 2)].isInput = ASIOFalse;
    buffer_infos[static_cast<std::size_t>(index + 2)].channelNum = index;
  }
  ASIOCallbacks callbacks{
      &buffer_switch,
      &sample_rate_changed,
      &asio_message,
      &buffer_switch_time_info,
  };
  assert(asio->createBuffers(buffer_infos.data(),
                             static_cast<long>(buffer_infos.size()),
                             preferred, &callbacks) == ASE_OK);
  for (const auto& buffer : buffer_infos) {
    assert(buffer.buffers[0] != nullptr);
    assert(buffer.buffers[1] != nullptr);
    assert(buffer.buffers[0] != buffer.buffers[1]);
  }
  channel_info = {};
  channel_info.channel = 0;
  channel_info.isInput = ASIOTrue;
  assert(asio->getChannelInfo(&channel_info) == ASE_OK);
  assert(channel_info.isActive == ASIOTrue);
  assert(asio->start() == ASE_OK);
  assert(asio->setSampleRate(48000.0) == ASE_InvalidMode);
  ASIOSamples sample_position{};
  ASIOTimeStamp timestamp{};
  assert(asio->getSamplePosition(&sample_position, &timestamp) == ASE_OK);
  assert(asio->disposeBuffers() == ASE_InvalidMode);
  assert(asio->stop() == ASE_OK);
  assert(asio->disposeBuffers() == ASE_OK);
  assert(asio->disposeBuffers() == ASE_InvalidMode);

  assert(factory->Release() == 0);
  assert(can_unload() == S_FALSE);
  assert(driver->Release() == 0);
  assert(can_unload() == S_OK);

  factory = nullptr;
  assert(get_class(sar::driver::kWindowsVirtualAsioClsid,
                   IID_IClassFactory,
                   reinterpret_cast<void**>(&factory)) == S_OK);
  assert(factory->LockServer(TRUE) == S_OK);
  assert(factory->Release() == 0);
  assert(can_unload() == S_FALSE);
  factory = nullptr;
  assert(get_class(sar::driver::kWindowsVirtualAsioClsid,
                   IID_IClassFactory,
                   reinterpret_cast<void**>(&factory)) == S_OK);
  assert(factory->LockServer(FALSE) == S_OK);
  assert(factory->Release() == 0);
  assert(can_unload() == S_OK);

  assert(FreeLibrary(module));
}
