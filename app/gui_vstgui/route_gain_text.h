#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace sar::gui_vstgui {
inline std::string routeGainText(float gain) {
  if (!std::isfinite(gain) || gain < 0.0F) return "-- dB";
  if (gain == 0.0F) return "-inf dB";
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%.1f dB", 20.0 * std::log10(gain));
  return buffer;
}
}
