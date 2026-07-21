#include "driver/windows_virtual_asio_com.h"

#include <Windows.h>
#include <objbase.h>

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

std::atomic_ulong callback_count = 0;

void buffer_switch(long, ASIOBool) {
  callback_count.fetch_add(1, std::memory_order_relaxed);
}

void sample_rate_changed(ASIOSampleRate) {}

long asio_message(long selector, long, void*, double*) {
  return selector == kAsioSelectorSupported ? 1 : 0;
}

ASIOTime* buffer_switch_time_info(ASIOTime* time, long index, ASIOBool direct) {
  buffer_switch(index, direct);
  return time;
}

bool parse_long(std::string_view text, long& value) {
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end && value > 0;
}

void print_error(IASIO& asio, std::string_view operation, ASIOError result) {
  char message[124] = {};
  asio.getErrorMessage(message);
  std::cerr << "asio_probe=" << operation << " result=" << result
            << " message=\"" << message << "\"\n";
}

class ComApartment {
 public:
  ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
  ~ComApartment() {
    if (SUCCEEDED(result_)) {
      CoUninitialize();
    }
  }
  [[nodiscard]] bool ok() const noexcept {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }

 private:
  HRESULT result_ = E_FAIL;
};

}  // namespace

int main(int argc, char** argv) {
  long sample_rate = 48000;
  long buffer_frames = 128;
  if ((argc > 1 && !parse_long(argv[1], sample_rate)) ||
      (argc > 2 && !parse_long(argv[2], buffer_frames)) || argc > 3) {
    std::cerr << "Usage: sar_virtual_asio_host_probe [sample-rate] [buffer-frames]\n";
    return 2;
  }

  ComApartment apartment;
  if (!apartment.ok()) {
    std::cerr << "asio_probe=com_initialize result=failed\n";
    return 1;
  }

  IUnknown* unknown = nullptr;
  const auto created = CoCreateInstance(
      sar::driver::kWindowsVirtualAsioClsid, nullptr, CLSCTX_INPROC_SERVER,
      sar::driver::kWindowsVirtualAsioClsid,
      reinterpret_cast<void**>(&unknown));
  if (FAILED(created) || unknown == nullptr) {
    std::cerr << "asio_probe=create_instance hresult=0x" << std::hex
              << static_cast<unsigned long>(created) << std::dec << '\n';
    return 1;
  }
  auto* asio = static_cast<IASIO*>(unknown);

  char name[32] = {};
  asio->getDriverName(name);
  std::cout << "asio_probe=loaded name=\"" << name << "\" version="
            << asio->getDriverVersion() << '\n';
  if (asio->init(GetDesktopWindow()) != ASIOTrue) {
    print_error(*asio, "init", ASE_NotPresent);
    unknown->Release();
    return 1;
  }

  long inputs = 0;
  long outputs = 0;
  auto result = asio->getChannels(&inputs, &outputs);
  if (result != ASE_OK) {
    print_error(*asio, "get_channels", result);
    unknown->Release();
    return 1;
  }
  long minimum = 0;
  long maximum = 0;
  long preferred = 0;
  long granularity = 0;
  result = asio->getBufferSize(&minimum, &maximum, &preferred, &granularity);
  if (result != ASE_OK) {
    print_error(*asio, "get_buffer_size", result);
    unknown->Release();
    return 1;
  }
  std::cout << "asio_probe=enumerated inputs=" << inputs
            << " outputs=" << outputs << " minimum=" << minimum
            << " maximum=" << maximum << " preferred=" << preferred
            << " granularity=" << granularity << '\n';

  result = asio->setSampleRate(static_cast<ASIOSampleRate>(sample_rate));
  if (result != ASE_OK) {
    print_error(*asio, "set_sample_rate", result);
    unknown->Release();
    return 1;
  }

  std::array<ASIOBufferInfo, 4> buffers{};
  for (long channel = 0; channel < 2; ++channel) {
    buffers[static_cast<std::size_t>(channel)].isInput = ASIOTrue;
    buffers[static_cast<std::size_t>(channel)].channelNum = channel;
    buffers[static_cast<std::size_t>(channel + 2)].isInput = ASIOFalse;
    buffers[static_cast<std::size_t>(channel + 2)].channelNum = channel;
  }
  ASIOCallbacks callbacks{
      &buffer_switch,
      &sample_rate_changed,
      &asio_message,
      &buffer_switch_time_info,
  };
  result = asio->createBuffers(buffers.data(),
                               static_cast<long>(buffers.size()),
                               buffer_frames, &callbacks);
  if (result != ASE_OK) {
    print_error(*asio, "create_buffers", result);
    unknown->Release();
    return 1;
  }
  std::cout << "asio_probe=buffers_created sample_rate=" << sample_rate
            << " frames=" << buffer_frames << '\n';

  result = asio->start();
  if (result != ASE_OK) {
    print_error(*asio, "start", result);
    asio->disposeBuffers();
    unknown->Release();
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  const auto callbacks_seen = callback_count.load(std::memory_order_acquire);
  std::cout << "asio_probe=streaming callbacks=" << callbacks_seen << '\n';
  const auto stop_result = asio->stop();
  const auto dispose_result = asio->disposeBuffers();
  unknown->Release();
  if (stop_result != ASE_OK || dispose_result != ASE_OK || callbacks_seen == 0) {
    std::cerr << "asio_probe=shutdown stop=" << stop_result
              << " dispose=" << dispose_result
              << " callbacks=" << callbacks_seen << '\n';
    return 1;
  }
  std::cout << "asio_probe=passed\n";
  return 0;
}
