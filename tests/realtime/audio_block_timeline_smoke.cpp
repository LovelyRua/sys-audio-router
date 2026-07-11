#include "core/realtime/audio_block_timeline.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

int main() {
  using sar::realtime::AudioBlockRelation;
  using sar::realtime::AudioBlockTimeline;
  using sar::realtime::ClockDomain;

  static_assert(std::is_trivially_copyable_v<ClockDomain>);
  static_assert(std::is_trivially_copyable_v<AudioBlockTimeline>);

  constexpr ClockDomain capture_clock{1, 48000};
  constexpr ClockDomain render_clock{2, 48000};
  static_assert(capture_clock.valid());
  static_assert(!ClockDomain{}.valid());

  constexpr AudioBlockTimeline first{capture_clock, 960, 480};
  constexpr AudioBlockTimeline contiguous{capture_clock, 1440, 480};
  constexpr AudioBlockTimeline overlap{capture_clock, 1200, 480};
  constexpr AudioBlockTimeline gap{capture_clock, 1920, 480};
  constexpr AudioBlockTimeline other_domain{render_clock, 1440, 480};

  static_assert(first.valid());
  static_assert(first.end_frame() == 1440);
  static_assert(first.relation_to(contiguous) == AudioBlockRelation::contiguous);
  static_assert(first.relation_to(overlap) == AudioBlockRelation::overlap);
  static_assert(first.relation_to(gap) == AudioBlockRelation::gap);
  static_assert(first.relation_to(other_domain) ==
                AudioBlockRelation::different_clock_domain);

  constexpr AudioBlockTimeline empty{capture_clock, 0, 0};
  constexpr AudioBlockTimeline overflowing{
      capture_clock, std::numeric_limits<std::uint64_t>::max(), 1};
  static_assert(!empty.valid());
  static_assert(!overflowing.valid());
  static_assert(first.relation_to(empty) == AudioBlockRelation::invalid);

  std::cout << "Audio block timeline smoke test passed\n";
  return 0;
}
