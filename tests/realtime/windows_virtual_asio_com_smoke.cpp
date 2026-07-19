#include "driver/windows_virtual_asio_com.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Unknwn.h>

#include <cassert>
#include <string>

namespace {

using DllCanUnloadNowFunction = HRESULT(STDAPICALLTYPE*)();
using DllGetClassObjectFunction = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID,
                                                          void**);

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
