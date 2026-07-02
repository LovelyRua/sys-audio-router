#pragma once

#include <cstddef>
#include <cstdint>

namespace sar::realtime {

struct ProcessContext {
  std::uint32_t sample_rate = 48000;
  std::size_t frames = 0;
  std::uint64_t block_index = 0;
};

}  // namespace sar::realtime

