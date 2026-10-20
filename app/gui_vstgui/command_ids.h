#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace sar::gui_vstgui {

class GuiCommandIds {
 public:
  GuiCommandIds()
      : prefix_("gui-vstgui-" + std::to_string(GetCurrentProcessId()) + "-" +
          std::to_string(GetTickCount64()) + "-" +
          std::to_string(clientSequence_.fetch_add(1, std::memory_order_relaxed)) + "-") {}

  std::string next() {
    return prefix_ + std::to_string(sequence_.fetch_add(1, std::memory_order_relaxed));
  }

 private:
  // The engine caches command IDs across all GUI windows. Both client identity
  // and request sequence must survive overlapping requests and window reopen.
  inline static std::atomic<std::uint64_t> clientSequence_{0};
  const std::string prefix_;
  std::atomic<std::uint64_t> sequence_{0};
};

}  // namespace sar::gui_vstgui
