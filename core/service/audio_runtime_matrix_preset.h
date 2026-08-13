#pragma once

#include "core/control/control_command.h"
#include "core/control/preset_document.h"

#include <vector>

namespace sar::service {

class AudioRuntimeMatrixPresetResult {
 public:
  static AudioRuntimeMatrixPresetResult success(
      control::PresetDocument preset);
  static AudioRuntimeMatrixPresetResult failure(
      std::vector<control::PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const control::PresetDocument& preset() const noexcept;
  [[nodiscard]] control::PresetDocument take_preset() noexcept;
  [[nodiscard]] const std::vector<control::PresetError>& errors()
      const noexcept;

 private:
  AudioRuntimeMatrixPresetResult(
      control::PresetDocument preset,
      std::vector<control::PresetError> errors) noexcept;

  control::PresetDocument preset_;
  std::vector<control::PresetError> errors_;
};

// Replaces physical runtime ports while retaining non-runtime ports and every
// route whose two endpoints remain present in the candidate topology.
[[nodiscard]] AudioRuntimeMatrixPresetResult
reconcile_audio_runtime_matrix_preset(
    const control::PresetDocument& current,
    const control::AudioRuntimeConfiguration& previous_configuration,
    const control::AudioRuntimeConfiguration& next_configuration);

}  // namespace sar::service
