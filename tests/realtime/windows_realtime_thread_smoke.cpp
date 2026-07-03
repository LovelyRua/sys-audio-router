#include "core/platform/windows_realtime_thread.h"

#include <iostream>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  sar::platform::WindowsRealtimeThreadScope scope;
  const auto result = sar::platform::WindowsRealtimeThreadScope::enter_current_thread(scope);
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }
  if (const auto failure = expect(scope.active(), "Expected active MMCSS scope")) {
    return failure;
  }

  scope.reset();
  if (const auto failure = expect(!scope.active(), "Expected inactive MMCSS scope after reset")) {
    return failure;
  }

  std::cout << "Windows realtime thread smoke test passed\n";
  return 0;
}
