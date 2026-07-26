#include "core/platform/windows_wasapi_endpoint_notification.h"

#include <mmdeviceapi.h>
#include <windows.h>

#include <atomic>
#include <new>

namespace sar::platform {
namespace {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<ULONG>::is_always_lock_free);

class EndpointNotificationClient final : public IMMNotificationClient {
 public:
  EndpointNotificationClient(std::uint64_t capture_generation,
                             std::uint64_t render_generation,
                             HANDLE change_event) noexcept
      : capture_generation_(capture_generation),
        render_generation_(render_generation),
        change_event_(change_event) {}

  ~EndpointNotificationClient() {
    if (change_event_ != nullptr) {
      CloseHandle(change_event_);
    }
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interface_id,
                                           void** object) noexcept override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    if (interface_id == __uuidof(IUnknown) ||
        interface_id == __uuidof(IMMNotificationClient)) {
      *object = static_cast<IMMNotificationClient*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() noexcept override {
    return reference_count_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const auto remaining =
        reference_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,
                                                 DWORD) noexcept override {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) noexcept override {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) noexcept override {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,
                                                   ERole role,
                                                   LPCWSTR) noexcept override {
    if (role != eConsole) {
      return S_OK;
    }
    if (flow == eCapture || flow == eAll) {
      capture_generation_.fetch_add(1, std::memory_order_release);
    }
    if (flow == eRender || flow == eAll) {
      render_generation_.fetch_add(1, std::memory_order_release);
    }
    if (flow == eCapture || flow == eRender || flow == eAll) {
      SetEvent(change_event_);
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
      LPCWSTR,
      const PROPERTYKEY) noexcept override {
    return S_OK;
  }

  [[nodiscard]] std::uint64_t capture_generation() const noexcept {
    return capture_generation_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::uint64_t render_generation() const noexcept {
    return render_generation_.load(std::memory_order_acquire);
  }

 private:
  std::atomic<ULONG> reference_count_{1};
  std::atomic<std::uint64_t> capture_generation_;
  std::atomic<std::uint64_t> render_generation_;
  HANDLE change_event_ = nullptr;
};

IMMDeviceEnumerator* as_enumerator(void* enumerator) noexcept {
  return static_cast<IMMDeviceEnumerator*>(enumerator);
}

EndpointNotificationClient* as_client(void* client) noexcept {
  return static_cast<EndpointNotificationClient*>(client);
}

}  // namespace

WindowsWasapiEndpointNotification::~WindowsWasapiEndpointNotification() {
  (void)unregister_notifications();
}

std::int32_t WindowsWasapiEndpointNotification::register_notifications() noexcept {
  if (registered()) {
    return owner_thread_id_.load(std::memory_order_acquire) ==
                   GetCurrentThreadId()
               ? S_FALSE
               : RPC_E_WRONG_THREAD;
  }

  const auto change_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (change_event == nullptr) {
    return HRESULT_FROM_WIN32(GetLastError());
  }

  auto* client = new (std::nothrow) EndpointNotificationClient(
      capture_generation_.load(std::memory_order_acquire),
      render_generation_.load(std::memory_order_acquire), change_event);
  if (client == nullptr) {
    CloseHandle(change_event);
    return E_OUTOFMEMORY;
  }

  IMMDeviceEnumerator* enumerator = nullptr;
  const auto create_result = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                              nullptr,
                                              CLSCTX_ALL,
                                              __uuidof(IMMDeviceEnumerator),
                                              reinterpret_cast<void**>(&enumerator));
  if (FAILED(create_result)) {
    client->Release();
    return create_result;
  }

  const auto register_result =
      enumerator->RegisterEndpointNotificationCallback(client);
  if (FAILED(register_result)) {
    enumerator->Release();
    client->Release();
    return register_result;
  }

  change_event_ = change_event;
  enumerator_ = enumerator;
  client_ = client;
  owner_thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
  return S_OK;
}

std::int32_t WindowsWasapiEndpointNotification::unregister_notifications() noexcept {
  if (!registered()) {
    return S_FALSE;
  }
  if (owner_thread_id_.load(std::memory_order_acquire) !=
      GetCurrentThreadId()) {
    return RPC_E_WRONG_THREAD;
  }

  auto* enumerator = as_enumerator(enumerator_);
  auto* client = as_client(client_);
  const auto unregister_result =
      enumerator->UnregisterEndpointNotificationCallback(client);

  return finish_unregistration(unregister_result);
}

std::int32_t WindowsWasapiEndpointNotification::finish_unregistration(
    std::int32_t unregister_result) noexcept {
  if (FAILED(unregister_result)) {
    // The callback may still be registered. Keep its owner reference, event, and
    // enumerator alive so a later callback cannot target released storage.
    return unregister_result;
  }

  auto* enumerator = as_enumerator(enumerator_);
  auto* client = as_client(client_);
  capture_generation_.store(client->capture_generation(),
                            std::memory_order_release);
  render_generation_.store(client->render_generation(),
                           std::memory_order_release);
  enumerator->Release();
  client->Release();
  enumerator_ = nullptr;
  client_ = nullptr;
  change_event_ = nullptr;
  owner_thread_id_.store(0, std::memory_order_release);
  return unregister_result;
}

bool WindowsWasapiEndpointNotification::registered() const noexcept {
  return enumerator_ != nullptr;
}

void* WindowsWasapiEndpointNotification::change_event() const noexcept {
  return change_event_;
}

bool WindowsWasapiEndpointNotification::reset_change_event() noexcept {
  return change_event_ != nullptr &&
         ResetEvent(static_cast<HANDLE>(change_event_)) != FALSE;
}

WasapiEndpointNotificationSnapshot
WindowsWasapiEndpointNotification::consume_snapshot() noexcept {
  WasapiEndpointNotificationSnapshot snapshot{
      .capture_generation = capture_generation(),
      .render_generation = render_generation(),
      .event_reset_succeeded = false,
  };

  snapshot.event_reset_succeeded = reset_change_event();

  // Re-read after resetting so a callback that raced with the first reads is
  // included here. A callback after ResetEvent leaves the event signaled for
  // the control thread's next poll.
  snapshot.capture_generation = capture_generation();
  snapshot.render_generation = render_generation();
  return snapshot;
}

std::uint64_t WindowsWasapiEndpointNotification::capture_generation() const noexcept {
  return client_ != nullptr ? as_client(client_)->capture_generation()
                            : capture_generation_.load(std::memory_order_acquire);
}

std::uint64_t WindowsWasapiEndpointNotification::render_generation() const noexcept {
  return client_ != nullptr ? as_client(client_)->render_generation()
                            : render_generation_.load(std::memory_order_acquire);
}

std::int32_t WindowsWasapiEndpointNotification::notify_default_device_for_test(
    std::int32_t data_flow, std::int32_t role) noexcept {
  if (client_ == nullptr) {
    return E_UNEXPECTED;
  }
  return as_client(client_)->OnDefaultDeviceChanged(
      static_cast<EDataFlow>(data_flow), static_cast<ERole>(role), nullptr);
}

std::int32_t
WindowsWasapiEndpointNotification::retain_failed_unregistration_for_test(
    std::int32_t unregister_result) noexcept {
  if (!registered()) {
    return E_UNEXPECTED;
  }
  if (SUCCEEDED(unregister_result)) {
    return E_INVALIDARG;
  }
  return finish_unregistration(unregister_result);
}

}  // namespace sar::platform
