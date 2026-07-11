#include "core/realtime/planar_audio_fifo.h"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

void fill(sar::realtime::AudioBuffer& buffer, float base) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      buffer.channel(channel)[frame] =
          base + static_cast<float>(channel * 1000 + frame);
    }
  }
}

void expect_frame(const sar::realtime::AudioBuffer& buffer,
                  std::size_t frame,
                  float base,
                  std::size_t source_frame) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    assert(buffer.channel(channel)[frame] ==
           base + static_cast<float>(channel * 1000 + source_frame));
  }
}

}  // namespace

int main() {
  using sar::realtime::AudioBuffer;
  using sar::realtime::PlanarAudioFifo;

  static_assert(noexcept(std::declval<PlanarAudioFifo&>().push(
      std::declval<const AudioBuffer&>(), 1)));
  static_assert(noexcept(std::declval<PlanarAudioFifo&>().pop(
      std::declval<AudioBuffer&>(), 1)));
  static_assert(noexcept(std::declval<const PlanarAudioFifo&>().peek(
      std::declval<AudioBuffer&>(), 1)));
  static_assert(noexcept(std::declval<PlanarAudioFifo&>().consume(1)));

  bool rejected = false;
  try {
    PlanarAudioFifo invalid(0, 4);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);

  PlanarAudioFifo fifo(2, 5);
  assert(fifo.channels() == 2);
  assert(fifo.capacity_frames() == 5);
  assert(fifo.available_frames() == 0);
  assert(fifo.free_frames() == 5);

  AudioBuffer first(2, 4);
  AudioBuffer second(2, 4);
  AudioBuffer output(2, 8);
  fill(first, 10.0F);
  fill(second, 20.0F);
  output.clear();

  assert(fifo.push(first, 0) == 0);
  assert(fifo.pop(output, 0) == 0);
  assert(fifo.push(first, 3) == 3);
  assert(fifo.pop(output, 2) == 2);
  expect_frame(output, 0, 10.0F, 0);
  expect_frame(output, 1, 10.0F, 1);

  // This write wraps and is shortened to the remaining free space.
  assert(fifo.push(second, 99) == 4);
  assert(fifo.available_frames() == 5);
  assert(fifo.free_frames() == 0);
  assert(fifo.push(first, 1) == 0);
  output.clear();
  assert(fifo.pop(output, 8) == 5);
  expect_frame(output, 0, 10.0F, 2);
  for (std::size_t frame = 0; frame < 4; ++frame) {
    expect_frame(output, frame + 1, 20.0F, frame);
  }

  AudioBuffer wrong_channels(1, 4);
  assert(fifo.push(wrong_channels, 4) == 0);
  assert(fifo.pop(wrong_channels, 4) == 0);

  // Build a wrapped read region for the two-stage render contract.
  fifo.clear();
  assert(fifo.push(first, 4) == 4);
  assert(fifo.consume(3) == 3);
  assert(fifo.push(second, 4) == 4);
  output.clear();
  assert(fifo.peek(output, 8) == 5);
  assert(fifo.available_frames() == 5);
  expect_frame(output, 0, 10.0F, 3);
  for (std::size_t frame = 0; frame < 4; ++frame) {
    expect_frame(output, frame + 1, 20.0F, frame);
  }

  AudioBuffer repeated(2, 8);
  repeated.clear();
  assert(fifo.peek(repeated, 5) == 5);
  for (std::size_t channel = 0; channel < 2; ++channel) {
    for (std::size_t frame = 0; frame < 5; ++frame) {
      assert(repeated.channel(channel)[frame] == output.channel(channel)[frame]);
    }
  }
  assert(fifo.peek(wrong_channels, 4) == 0);
  assert(fifo.available_frames() == 5);

  assert(fifo.consume(2) == 2);
  assert(fifo.available_frames() == 3);
  output.clear();
  assert(fifo.peek(output, 8) == 3);
  for (std::size_t frame = 0; frame < 3; ++frame) {
    expect_frame(output, frame, 20.0F, frame + 1);
  }
  assert(fifo.consume(99) == 3);
  assert(fifo.consume(1) == 0);

  AudioBuffer small(2, 2);
  fill(small, 30.0F);
  assert(fifo.push(small, 5) == 2);
  output.clear();
  assert(fifo.pop(output, 1) == 1);
  fifo.clear();
  assert(fifo.available_frames() == 0);
  assert(fifo.free_frames() == 5);

  // Exercise index wraparound repeatedly while preserving channel alignment.
  AudioBuffer cycle_input(2, 3);
  AudioBuffer cycle_output(2, 3);
  for (std::size_t cycle = 0; cycle < 10'000; ++cycle) {
    const auto base = static_cast<float>(cycle * 10);
    fill(cycle_input, base);
    assert(fifo.push(cycle_input, 3) == 3);
    assert(fifo.pop(cycle_output, 3) == 3);
    for (std::size_t frame = 0; frame < 3; ++frame) {
      expect_frame(cycle_output, frame, base, frame);
    }
  }

  std::cout << "Planar audio FIFO smoke test passed\n";
  return 0;
}
