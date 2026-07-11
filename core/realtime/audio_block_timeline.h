#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace sar::realtime {

struct ClockDomain {
  std::uint64_t id = 0;
  std::uint32_t nominal_sample_rate = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return id != 0 && nominal_sample_rate != 0;
  }

  friend constexpr bool operator==(const ClockDomain&,
                                   const ClockDomain&) noexcept = default;
};

enum class AudioBlockRelation {
  invalid,
  different_clock_domain,
  overlap,
  contiguous,
  gap,
};

struct AudioBlockTimeline {
  ClockDomain clock_domain;
  std::uint64_t start_frame = 0;
  std::uint32_t frame_count = 0;

  [[nodiscard]] constexpr std::optional<std::uint64_t> end_frame() const noexcept {
    if (!clock_domain.valid() || frame_count == 0 ||
        start_frame > std::numeric_limits<std::uint64_t>::max() - frame_count) {
      return std::nullopt;
    }
    return start_frame + frame_count;
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return end_frame().has_value();
  }

  [[nodiscard]] constexpr AudioBlockRelation relation_to(
      const AudioBlockTimeline& next) const noexcept {
    const auto current_end = end_frame();
    if (!current_end.has_value() || !next.valid()) {
      return AudioBlockRelation::invalid;
    }
    if (clock_domain != next.clock_domain) {
      return AudioBlockRelation::different_clock_domain;
    }
    if (next.start_frame < *current_end) {
      return AudioBlockRelation::overlap;
    }
    if (next.start_frame == *current_end) {
      return AudioBlockRelation::contiguous;
    }
    return AudioBlockRelation::gap;
  }
};

}  // namespace sar::realtime
