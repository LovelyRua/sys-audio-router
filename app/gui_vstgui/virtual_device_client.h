#pragma once

#include <string>
#include <vector>

namespace sar::gui_vstgui {

struct VirtualDeviceInstance {
  std::string id;
  std::string label;
  bool started = false;
  bool problem = false;
  unsigned long problemCode = 0;
};

struct VirtualDeviceSnapshot {
  bool available = false;
  unsigned maximum = 0;
  std::vector<VirtualDeviceInstance> instances;
  std::string error;
};

enum class VirtualDeviceAction { Add, Rename, Remove };

struct VirtualDeviceResult {
  bool success = false;
  bool cancelled = false;
  bool outcomeUnknown = false;
  std::string message;
  VirtualDeviceSnapshot snapshot;
};

// Independent executable boundary. No driver headers or private transport are
// linked into the control panel. Call these blocking methods off the UI thread.
class VirtualDeviceClient final {
 public:
  VirtualDeviceClient();
  [[nodiscard]] VirtualDeviceSnapshot inspect() const;
  [[nodiscard]] VirtualDeviceResult execute(VirtualDeviceAction action,
      const std::string& instanceId, const std::string& label,
      const std::string& infPath) const;
};

// Strict UTF-8 JSON emitted by the independent manager's list --json command.
[[nodiscard]] VirtualDeviceSnapshot parseVirtualDeviceSnapshot(const std::string& json);

}  // namespace sar::gui_vstgui
