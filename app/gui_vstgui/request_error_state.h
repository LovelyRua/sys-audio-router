#pragma once

#include <string>

namespace sar::gui_vstgui {

class GuiRequestErrorState {
 public:
  void observe(const std::string& error, bool transportOk) {
    if (!error.empty()) {
      message_ = error;
      transient_ = !transportOk;
    } else if (transportOk && transient_) {
      clear();
    }
  }
  void clear() { message_.clear(); transient_ = false; }
  const std::string& message() const { return message_; }

 private:
  std::string message_;
  bool transient_ = false;
};

}  // namespace sar::gui_vstgui
