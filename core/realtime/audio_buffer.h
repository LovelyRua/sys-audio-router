#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace sar::realtime {

class AudioBuffer {
 public:
  AudioBuffer(std::size_t channels, std::size_t frames);

  [[nodiscard]] std::size_t channels() const noexcept;
  [[nodiscard]] std::size_t frames() const noexcept;
  [[nodiscard]] std::span<float> channel(std::size_t index) noexcept;
  [[nodiscard]] std::span<const float> channel(std::size_t index) const noexcept;

  void clear() noexcept;
  void copy_from(const AudioBuffer& source) noexcept;

 private:
  std::size_t channels_;
  std::size_t frames_;
  std::vector<float> samples_;
};

}  // namespace sar::realtime
