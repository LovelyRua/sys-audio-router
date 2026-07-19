#include "driver/windows_virtual_asio_com.h"

#include <Unknwn.h>

#include <atomic>
#include <new>

namespace {

std::atomic_ulong module_objects = 0;
std::atomic_ulong server_locks = 0;

class VirtualAsioDriver final : public IUnknown {
 public:
  VirtualAsioDriver() noexcept { module_objects.fetch_add(1); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                           void** object) noexcept override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    if (!IsEqualIID(iid, IID_IUnknown)) {
      return E_NOINTERFACE;
    }
    *object = static_cast<IUnknown*>(this);
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() noexcept override {
    return references_.fetch_add(1) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const auto remaining = references_.fetch_sub(1) - 1;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

 private:
  ~VirtualAsioDriver() { module_objects.fetch_sub(1); }

  std::atomic_ulong references_ = 1;
};

class VirtualAsioClassFactory final : public IClassFactory {
 public:
  VirtualAsioClassFactory() noexcept { module_objects.fetch_add(1); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                           void** object) noexcept override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    if (!IsEqualIID(iid, IID_IUnknown) &&
        !IsEqualIID(iid, IID_IClassFactory)) {
      return E_NOINTERFACE;
    }
    *object = static_cast<IClassFactory*>(this);
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() noexcept override {
    return references_.fetch_add(1) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const auto remaining = references_.fetch_sub(1) - 1;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,
                                           REFIID iid,
                                           void** object) noexcept override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    if (outer != nullptr) {
      return CLASS_E_NOAGGREGATION;
    }
    auto* driver = new (std::nothrow) VirtualAsioDriver();
    if (driver == nullptr) {
      return E_OUTOFMEMORY;
    }
    const auto result = driver->QueryInterface(iid, object);
    driver->Release();
    return result;
  }

  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) noexcept override {
    if (lock) {
      server_locks.fetch_add(1);
    } else {
      auto current = server_locks.load();
      while (current != 0 &&
             !server_locks.compare_exchange_weak(current, current - 1)) {
      }
    }
    return S_OK;
  }

 private:
  ~VirtualAsioClassFactory() { module_objects.fetch_sub(1); }

  std::atomic_ulong references_ = 1;
};

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance,
                               DWORD reason,
                               void*) noexcept {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

STDAPI DllCanUnloadNow() {
  return module_objects.load() == 0 && server_locks.load() == 0 ? S_OK
                                                                : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid,
                         REFIID iid,
                         void** object) {
  if (object == nullptr) {
    return E_POINTER;
  }
  *object = nullptr;
  if (!IsEqualCLSID(clsid, sar::driver::kWindowsVirtualAsioClsid)) {
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  auto* factory = new (std::nothrow) VirtualAsioClassFactory();
  if (factory == nullptr) {
    return E_OUTOFMEMORY;
  }
  const auto result = factory->QueryInterface(iid, object);
  factory->Release();
  return result;
}
