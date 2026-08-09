#include "driver/windows_virtual_asio_com.h"
#include "driver/windows_virtual_asio_runtime.h"
#include "core/graph/graph.h"
#include "core/service/windows_virtual_asio_broker_server.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Unknwn.h>

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

namespace {

using DllCanUnloadNowFunction = HRESULT(STDAPICALLTYPE*)();
using DllGetClassObjectFunction = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID,
                                                          void**);

std::array<ASIOBufferInfo, 4>* active_buffers = nullptr;
std::atomic_uint32_t callback_count = 0;
std::atomic_uint32_t time_info_callback_count = 0;
std::atomic_uint32_t time_info_query_count = 0;
std::atomic_bool round_trip_observed = false;
std::atomic_bool valid_time_info_observed = false;
std::atomic_uint32_t minimum_round_trip_callback_lag = UINT32_MAX;

std::unique_ptr<sar::graph::Graph> make_graph(
    const sar::platform::VirtualAsioFormat& format) {
  auto graph = std::make_unique<sar::graph::Graph>(
      1, format.input_channels, format.frames_per_block, format.sample_rate);
  graph->add_node(std::make_unique<sar::graph::GainNode>(0.25F));
  return graph;
}

void buffer_switch(long buffer_index, ASIOBool) {
  assert(active_buffers != nullptr);
  const auto half = static_cast<std::size_t>(buffer_index);
  const auto* input_left = static_cast<const float*>((*active_buffers)[0].buffers[half]);
  const auto* input_right = static_cast<const float*>((*active_buffers)[1].buffers[half]);
  const auto callback_number =
      callback_count.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (input_left[0] > 0.0F && input_right[0] > 0.0F) {
    const auto source_callback = static_cast<std::uint32_t>(std::lround(
        (input_left[0] * 4.0F - 0.4F) * 1000.0F));
    if (source_callback > 0 && source_callback < callback_number &&
        std::fabs(input_right[0] - (input_left[0] + 0.01F)) < 0.000001F) {
      const auto lag = callback_number - source_callback;
      auto minimum = minimum_round_trip_callback_lag.load();
      while (lag < minimum &&
             !minimum_round_trip_callback_lag.compare_exchange_weak(minimum,
                                                                    lag)) {
      }
      round_trip_observed.store(true, std::memory_order_release);
    }
  }
  auto* output_left = static_cast<float*>((*active_buffers)[2].buffers[half]);
  auto* output_right = static_cast<float*>((*active_buffers)[3].buffers[half]);
  const auto output_value =
      0.4F + static_cast<float>(callback_number) * 0.001F;
  for (std::size_t frame = 0; frame < 256; ++frame) {
    output_left[frame] = output_value;
    output_right[frame] = output_value + 0.04F;
  }
}
void sample_rate_changed(ASIOSampleRate) {}
long asio_message(long selector, long, void*, double*) {
  if (selector == kAsioSupportsTimeInfo) {
    time_info_query_count.fetch_add(1, std::memory_order_release);
    return 1;
  }
  return 0;
}
ASIOTime* buffer_switch_time_info(ASIOTime* time,
                                  long buffer_index,
                                  ASIOBool direct_process) {
  assert(time != nullptr);
  assert(direct_process == ASIOFalse);
  const auto required_flags =
      kSystemTimeValid | kSamplePositionValid | kSampleRateValid | kSpeedValid;
  if ((time->timeInfo.flags & required_flags) == required_flags &&
      time->timeInfo.sampleRate == 48000.0 &&
      time->timeInfo.speed == 1.0) {
    valid_time_info_observed.store(true, std::memory_order_release);
  }
  time_info_callback_count.fetch_add(1, std::memory_order_release);
  buffer_switch(buffer_index, direct_process);
  return time;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  using sar::driver::detail::advance_windows_virtual_asio_deadline;
  auto schedule = advance_windows_virtual_asio_deadline(100, 10, 106);
  assert(schedule.deadline_qpc == 110);
  assert(schedule.skipped_periods == 0);
  schedule = advance_windows_virtual_asio_deadline(
      schedule.deadline_qpc, 10, 116);
  assert(schedule.deadline_qpc == 120);
  assert(schedule.skipped_periods == 0);
  schedule = advance_windows_virtual_asio_deadline(
      schedule.deadline_qpc, 10, 155);
  assert(schedule.deadline_qpc == 160);
  assert(schedule.skipped_periods == 3);

  using sar::driver::detail::WindowsVirtualAsioRationalClock;
  constexpr std::uint64_t qpc_frequency = 10'000'000;
  constexpr std::uint64_t scheduler_sample_rate = 48'000;
  WindowsVirtualAsioRationalClock clock_128(
      (128 * qpc_frequency) / scheduler_sample_rate,
      (128 * qpc_frequency) % scheduler_sample_rate,
      scheduler_sample_rate);
  WindowsVirtualAsioRationalClock clock_256(
      (256 * qpc_frequency) / scheduler_sample_rate,
      (256 * qpc_frequency) % scheduler_sample_rate,
      scheduler_sample_rate);
  auto deadline_128 = clock_128.reset(1'000);
  auto deadline_256 = clock_256.reset(1'000);
  deadline_128 = clock_128.advance(deadline_128.deadline_qpc);
  assert(deadline_128.deadline_qpc == deadline_256.deadline_qpc);
  for (std::uint32_t pair = 1; pair < 100'000; ++pair) {
    deadline_128 = clock_128.advance(deadline_128.deadline_qpc);
    deadline_128 = clock_128.advance(deadline_128.deadline_qpc);
    deadline_256 = clock_256.advance(deadline_256.deadline_qpc);
    assert(deadline_128.deadline_qpc == deadline_256.deadline_qpc);
  }
  const auto expected_elapsed =
      (static_cast<std::uint64_t>(200'000) * 128 * qpc_frequency) /
      scheduler_sample_rate;
  assert(deadline_128.deadline_qpc == 1'000 + expected_elapsed);

  const auto late_now = deadline_128.deadline_qpc + 1'000'000;
  const auto recovered = clock_128.advance(late_now);
  assert(recovered.skipped_periods == 1);
  assert(recovered.deadline_qpc > late_now);
  const std::wstring pipe_name =
      L"sys-audio-route-asio-com-smoke-" +
      std::to_wstring(GetCurrentProcessId());
  sar::service::WindowsVirtualAsioTransportHost transport_host({
      .endpoint_token = "asio-com-smoke",
      .maximum_clients = 2,
      .queue_capacity_blocks = 8,
      .wait_timeout_ms = 10,
  });
  sar::service::WindowsVirtualAsioBrokerServer broker_server(
      pipe_name, transport_host, make_graph,
      [] { return sar::platform::VirtualAsioFormat{48000, 256, 2, 2}; });
  assert(broker_server.start().ok());
  assert(SetEnvironmentVariableW(L"SAR_VIRTUAL_ASIO_PIPE",
                                 pipe_name.c_str()));

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
  assert(factory->CreateInstance(nullptr,
                                 sar::driver::kWindowsVirtualAsioClsid,
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
  assert(asio->setSampleRate(96000.0) == ASE_NoClock);
  ASIOSampleRate sample_rate = 0;
  assert(asio->getSampleRate(&sample_rate) == ASE_OK);
  assert(sample_rate == 48000.0);
  assert(asio->future(kAsioCanTimeInfo, nullptr) == ASE_SUCCESS);
  assert(asio->future(kAsioCanTimeCode, nullptr) == ASE_NotPresent);

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
  active_buffers = &buffer_infos;
  assert(asio->createBuffers(buffer_infos.data(),
                             static_cast<long>(buffer_infos.size()),
                             preferred, &callbacks) == ASE_OK);
  assert(time_info_query_count.load(std::memory_order_acquire) == 1);
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
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!round_trip_observed.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(callback_count.load(std::memory_order_acquire) >= 2);
  assert(time_info_callback_count.load(std::memory_order_acquire) >= 2);
  assert(valid_time_info_observed.load(std::memory_order_acquire));
  assert(round_trip_observed.load(std::memory_order_acquire));
  assert(minimum_round_trip_callback_lag.load(std::memory_order_acquire) <= 2);
  assert(asio->setSampleRate(48000.0) == ASE_InvalidMode);
  ASIOSamples sample_position{};
  ASIOTimeStamp timestamp{};
  assert(asio->getSamplePosition(&sample_position, &timestamp) == ASE_OK);
  // Host shutdown may skip stop(). Buffer disposal must own the stop/join
  // sequence rather than leaving the broker callback thread behind.
  assert(asio->disposeBuffers() == ASE_OK);
  assert(asio->stop() == ASE_OK);
  const auto callbacks_after_stop = callback_count.load(std::memory_order_acquire);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  assert(callback_count.load(std::memory_order_acquire) == callbacks_after_stop);
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
  active_buffers = nullptr;
  assert(SetEnvironmentVariableW(L"SAR_VIRTUAL_ASIO_PIPE", nullptr));
  broker_server.stop();
}
