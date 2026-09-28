#pragma once

#include "core/platform/virtual_wasapi_ring_audio_source.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace sar::platform {

class WindowsVirtualWasapiRenderSource {
 public:
  struct OpenResult {
    std::unique_ptr<WindowsVirtualWasapiRenderSource> mapping;
    std::string error_code;
    std::uint32_t native_error = 0;

    [[nodiscard]] bool ok() const noexcept { return mapping != nullptr; }
  };

  [[nodiscard]] static OpenResult open(const std::wstring& object_name);

  WindowsVirtualWasapiRenderSource(const WindowsVirtualWasapiRenderSource&) = delete;
  WindowsVirtualWasapiRenderSource& operator=(const WindowsVirtualWasapiRenderSource&) = delete;
  ~WindowsVirtualWasapiRenderSource();

  [[nodiscard]] VirtualWasapiRingAudioSource& source() noexcept { return *source_; }
  [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }
  [[nodiscard]] std::uint32_t channels() const noexcept { return source_->channels(); }

 private:
  WindowsVirtualWasapiRenderSource(void* handle, void* view,
                                  std::unique_ptr<VirtualWasapiRingAudioSource> source,
                                  std::uint32_t sample_rate) noexcept;

  void* handle_ = nullptr;
  void* view_ = nullptr;
  std::unique_ptr<VirtualWasapiRingAudioSource> source_;
  std::uint32_t sample_rate_ = 0;
};

}  // namespace sar::platform
