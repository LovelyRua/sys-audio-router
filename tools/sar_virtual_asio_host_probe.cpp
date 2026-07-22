#include "driver/windows_virtual_asio_com.h"

#include <Windows.h>
#include <objbase.h>

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string_view>
#include <thread>

namespace {

std::atomic_ulong callback_count = 0;
ASIOBufferInfo* active_buffers = nullptr;
long active_buffer_count = 0;
long active_buffer_frames = 0;
double tone_phase = 0.0;
double tone_phase_increment = 0.0;
float tone_amplitude = 0.0F;

void buffer_switch(long buffer_index, ASIOBool) {
  if (active_buffers != nullptr && buffer_index >= 0 && buffer_index < 2) {
    for (long frame = 0; frame < active_buffer_frames; ++frame) {
      const auto sample = tone_amplitude == 0.0F
                              ? 0.0F
                              : tone_amplitude *
                                    static_cast<float>(std::sin(tone_phase));
      for (long buffer = 0; buffer < active_buffer_count; ++buffer) {
        auto& info = active_buffers[buffer];
        if (info.isInput == ASIOFalse && info.buffers[buffer_index] != nullptr) {
          static_cast<float*>(info.buffers[buffer_index])[frame] = sample;
        }
      }
      tone_phase += tone_phase_increment;
      if (tone_phase >= 2.0 * std::numbers::pi) {
        tone_phase -= 2.0 * std::numbers::pi;
      }
    }
  }
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

bool parse_double(std::string_view text, double& value) {
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end &&
         std::isfinite(value);
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
  long duration_ms = 500;
  double tone_hz = 0.0;
  double tone_level_dbfs = -18.0;
  if ((argc > 1 && !parse_long(argv[1], sample_rate)) ||
      (argc > 2 && !parse_long(argv[2], buffer_frames)) ||
      (argc > 3 && !parse_long(argv[3], duration_ms)) ||
      (argc > 4 && !parse_double(argv[4], tone_hz)) ||
      (argc > 5 && !parse_double(argv[5], tone_level_dbfs)) || argc > 6 ||
      tone_hz < 0.0 || tone_hz >= static_cast<double>(sample_rate) / 2.0 ||
      tone_level_dbfs > 0.0 || tone_level_dbfs < -120.0) {
    std::cerr << "Usage: sar_virtual_asio_host_probe [sample-rate] "
                 "[buffer-frames] [duration-ms] [tone-hz] "
                 "[tone-level-dbfs]\n";
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

  callback_count.store(0, std::memory_order_release);
  active_buffers = buffers.data();
  active_buffer_count = static_cast<long>(buffers.size());
  active_buffer_frames = buffer_frames;
  tone_phase = 0.0;
  tone_phase_increment = 2.0 * std::numbers::pi * tone_hz /
                         static_cast<double>(sample_rate);
  tone_amplitude = tone_hz == 0.0
                       ? 0.0F
                       : static_cast<float>(std::pow(10.0,
                                                     tone_level_dbfs / 20.0));
  std::cout << "asio_probe=signal tone_hz=" << tone_hz
            << " level_dbfs=" << tone_level_dbfs
            << " duration_ms=" << duration_ms << '\n';

  result = asio->start();
  if (result != ASE_OK) {
    print_error(*asio, "start", result);
    asio->disposeBuffers();
    unknown->Release();
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
  const auto callbacks_seen = callback_count.load(std::memory_order_acquire);
  const auto expected_callbacks =
      (static_cast<unsigned long long>(duration_ms) *
       static_cast<unsigned long long>(sample_rate)) /
      (1000ULL * static_cast<unsigned long long>(buffer_frames));
  const auto minimum_callbacks = std::max(1ULL, expected_callbacks * 8ULL / 10ULL);
  std::cout << "asio_probe=streaming callbacks=" << callbacks_seen
            << " expected=" << expected_callbacks
            << " minimum=" << minimum_callbacks << '\n';
  const auto stop_result = asio->stop();
  active_buffers = nullptr;
  active_buffer_count = 0;
  active_buffer_frames = 0;
  const auto dispose_result = asio->disposeBuffers();
  unknown->Release();
  if (stop_result != ASE_OK || dispose_result != ASE_OK ||
      callbacks_seen < minimum_callbacks) {
    std::cerr << "asio_probe=shutdown stop=" << stop_result
              << " dispose=" << dispose_result
              << " callbacks=" << callbacks_seen << '\n';
    return 1;
  }
  std::cout << "asio_probe=passed\n";
  return 0;
}
