#pragma once

#include <cstddef>
#include <vector>

#include "core/realtime/audio_buffer.h"

namespace sar::realtime {

// Single-threaded planar FIFO. Construction owns all storage; push/pop do not
// allocate or synchronize. The caller chooses how to handle short transfers.
class PlanarAudioFifo {
 public:
  PlanarAudioFifo(std::size_t channels, std::size_t capacity_frames);

  [[nodiscard]] std::size_t channels() const noexcept;
  [[nodiscard]] std::size_t capacity_frames() const noexcept;
  [[nodiscard]] std::size_t available_frames() const noexcept;
  [[nodiscard]] std::size_t free_frames() const noexcept;

  [[nodiscard]] std::size_t push(const AudioBuffer& source,
                                 std::size_t frames) noexcept;
  [[nodiscard]] std::size_t peek(AudioBuffer& destination,
                                 std::size_t frames) const noexcept;
  [[nodiscard]] std::size_t consume(std::size_t frames) noexcept;
  [[nodiscard]] std::size_t pop(AudioBuffer& destination,
                                std::size_t frames) noexcept;
  void clear() noexcept;

 private:
  std::size_t channels_;
  std::size_t capacity_frames_;
  std::vector<float> samples_;
  std::size_t read_frame_ = 0;
  std::size_t write_frame_ = 0;
  std::size_t available_frames_ = 0;
};

}  // namespace sar::realtime
