#include "core/platform/windows_virtual_asio_events.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

int main() {
  using namespace sar::platform;

  const auto token = "events-" + std::to_string(GetCurrentProcessId()) + "-" +
                     std::to_string(GetTickCount64());
  auto generated = make_windows_virtual_asio_object_names(token, "client", 1);
  assert(generated.ok());
  const auto names = generated.names();

  auto created = WindowsVirtualAsioEvents::create(names);
  assert(created.ok());
  auto owner = created.take_events();
  assert(owner->valid());
  assert(owner->owner());
  assert(owner->names() == names);

  auto duplicate = WindowsVirtualAsioEvents::create(names);
  assert(!duplicate.ok());
  assert(duplicate.errors().front().code ==
         "virtual_asio_event_already_exists");

  auto opened = WindowsVirtualAsioEvents::open(names);
  assert(opened.ok());
  auto client = opened.take_events();
  assert(client->valid());
  assert(!client->owner());

  assert(client->wait_input_or_shutdown(0).status ==
         WindowsVirtualAsioEventWaitStatus::TimedOut);
  assert(owner->signal_input());
  assert(client->wait_input_or_shutdown(100).status ==
         WindowsVirtualAsioEventWaitStatus::Ready);
  assert(client->wait_input_or_shutdown(0).status ==
         WindowsVirtualAsioEventWaitStatus::TimedOut);

  assert(client->signal_output());
  assert(owner->wait_output_or_shutdown(100).status ==
         WindowsVirtualAsioEventWaitStatus::Ready);
  assert(owner->wait_output_or_shutdown(0).status ==
         WindowsVirtualAsioEventWaitStatus::TimedOut);

  assert(client->signal_shutdown());
  assert(owner->wait_input_or_shutdown(0).status ==
         WindowsVirtualAsioEventWaitStatus::Shutdown);
  assert(owner->wait_output_or_shutdown(0).status ==
         WindowsVirtualAsioEventWaitStatus::Shutdown);
  assert(owner->reset_shutdown());
  assert(client->wait_input_or_shutdown(0).status ==
         WindowsVirtualAsioEventWaitStatus::TimedOut);

  std::atomic<WindowsVirtualAsioEventWaitStatus> threaded_status =
      WindowsVirtualAsioEventWaitStatus::Failed;
  std::thread waiter([&] {
    threaded_status.store(client->wait_input_or_shutdown(5000).status);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  assert(owner->signal_input());
  waiter.join();
  assert(threaded_status.load() == WindowsVirtualAsioEventWaitStatus::Ready);

  owner->close();
  assert(!owner->valid());
  assert(client->signal_input());
  auto still_open = WindowsVirtualAsioEvents::open(names);
  assert(still_open.ok());
  still_open.take_events()->close();
  client->close();
  assert(!client->signal_input());
  assert(client->wait_input_or_shutdown(0).status ==
         WindowsVirtualAsioEventWaitStatus::Failed);
  assert(!WindowsVirtualAsioEvents::open(names).ok());

  auto invalid_names = names;
  invalid_names.input_event = L"Global\\outside.input-event";
  auto invalid = WindowsVirtualAsioEvents::create(std::move(invalid_names));
  assert(!invalid.ok());
  assert(invalid.errors().front().code == "invalid_virtual_asio_event_names");
}
