#include "tools/capture_level_evidence.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

int main() {
  using sar::realtime::AudioBuffer;
  using sar::tools::CaptureLevelEvidence;
  CaptureLevelEvidence meter(2);
  AudioBuffer buffer(2, 8);
  buffer.clear();
  assert(meter.observe(buffer, 0));
  assert(meter.frames == 0 && meter.packets == 0);
  assert(meter.observe(buffer, 8));
  assert(meter.frames == 8 && meter.packets == 1 && meter.finite_samples == 16);
  assert(meter.peak == 0 && meter.rms() == 0 && meter.signal_frames == 0);
  assert(meter.first_signal_frame == CaptureLevelEvidence::kNoFrame);

  // An isolated stereo frame has a high peak but very low aggregate RMS.
  buffer.channel(0)[3] = 0.08F;
  buffer.channel(1)[3] = 0.08F;
  assert(meter.observe(buffer, 8));
  assert(meter.nonzero_samples == 2 && meter.nonzero_frames == 1);
  assert(meter.signal_frames == 1 && meter.signal_packets == 1);
  assert(meter.first_signal_frame == 11 && meter.last_signal_frame == 11);
  assert(meter.first_signal_packet == 1 && meter.peak_frame == 11);
  assert(std::fabs(meter.rms() - 0.08 / 4.0) < 1.0e-8);
  assert(meter.maximum_step_frame == 11 && meter.maximum_step_channel == 0);

  CaptureLevelEvidence boundary(2);
  buffer.clear();
  std::fill(buffer.channel(0).begin(), buffer.channel(0).end(), 0.25F);
  assert(boundary.observe(buffer, 8));
  buffer.clear();
  assert(boundary.observe(buffer, 8));
  assert(boundary.maximum_step == 0.25 && boundary.maximum_step_frame == 8);
  assert(boundary.nonzero_frames == 8 && boundary.signal_packets == 1);

  CaptureLevelEvidence partial(2);
  buffer.clear();
  buffer.channel(0)[7] = std::numeric_limits<float>::quiet_NaN();
  buffer.channel(1)[7] = 1.0F;
  assert(partial.observe(buffer, 4));
  assert(partial.frames == 4 && partial.nonfinite == 0 && partial.peak == 0);
  assert(!partial.observe(buffer, 9));
  assert(partial.frames == 4 && partial.packets == 1);
  AudioBuffer wrong_channels(1, 8);
  assert(!partial.observe(wrong_channels, 8));
  assert(partial.frames == 4);

  CaptureLevelEvidence invalid(1);
  AudioBuffer samples(1, 4);
  samples.channel(0)[0] = 0.1F;
  samples.channel(0)[1] = std::numeric_limits<float>::quiet_NaN();
  samples.channel(0)[2] = std::numeric_limits<float>::infinity();
  samples.channel(0)[3] = -0.1F;
  assert(invalid.observe(samples, 4));
  assert(invalid.nonfinite == 2 && invalid.finite_samples == 2);
  assert(invalid.nonzero_samples == 2 && invalid.signal_frames == 2);
  assert(invalid.maximum_step == 0); // Do not bridge an unknown/NaN interval.
  assert(std::fabs(invalid.rms() - 0.1) < 1.0e-8);

  CaptureLevelEvidence floor(1);
  samples.clear();
  samples.channel(0)[0] = 1.0e-6F;
  samples.channel(0)[1] = -1.0e-6F;
  assert(floor.observe(samples, 4));
  assert(floor.nonzero_frames == 2 && floor.signal_frames == 0);
  assert(floor.first_signal_frame == CaptureLevelEvidence::kNoFrame);
  samples.clear();
  samples.channel(0)[0] = CaptureLevelEvidence::kSignalThreshold;
  assert(floor.observe(samples, 1));
  assert(floor.signal_frames == 1 && floor.first_signal_frame == 4);

  CaptureLevelEvidence empty;
  AudioBuffer no_channels(0, 4);
  assert(!empty.observe(no_channels, 1));
  assert(empty.observe(no_channels, 0));
}
