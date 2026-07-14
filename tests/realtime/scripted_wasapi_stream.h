#pragma once

#include "core/platform/windows_wasapi_stream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace sar::tests {

class ScriptedWasapiStream final : public platform::WasapiStreamIo {
 public:
  struct CaptureStep {
    std::uint32_t frames = 0;
    std::vector<std::vector<float>> samples;
    platform::WasapiStreamIoStatus status = platform::WasapiStreamIoStatus::Completed;
    bool silent = false;
    bool data_discontinuity = false;
    bool timestamp_error = false;
    std::vector<platform::WasapiStreamError> errors;
  };

  struct RenderStep {
    std::uint32_t writable_frames = 0;
    platform::WasapiStreamIoStatus status = platform::WasapiStreamIoStatus::Completed;
    std::vector<platform::WasapiStreamError> errors;
  };

  struct RenderSubmission {
    std::uint32_t frames = 0;
    std::vector<std::vector<float>> samples;
  };

  explicit ScriptedWasapiStream(platform::WasapiStreamProbe probe)
      : probe_(std::move(probe)) {}

  void enqueue_capture(CaptureStep step) { capture_steps_.push_back(std::move(step)); }
  void enqueue_render(RenderStep step) { render_steps_.push_back(std::move(step)); }

  void set_io_call_log(std::vector<platform::WasapiStreamDirection>* call_log) {
    io_call_log_ = call_log;
  }

  void set_start_result(platform::WasapiStreamResult result) {
    start_result_ = std::move(result);
  }

  void set_stop_result(platform::WasapiStreamResult result) {
    stop_result_ = std::move(result);
  }

  [[nodiscard]] platform::WasapiStreamResult start() noexcept override {
    ++start_calls_;
    return start_result_;
  }

  [[nodiscard]] platform::WasapiStreamResult stop() noexcept override {
    ++stop_calls_;
    return stop_result_;
  }

  [[nodiscard]] platform::WasapiStreamIoResult render_once(
      const realtime::AudioBuffer& source,
      std::uint32_t frames,
      std::uint32_t) noexcept override {
    if (io_call_log_ != nullptr) {
      io_call_log_->push_back(platform::WasapiStreamDirection::Render);
    }
    if (render_steps_.empty()) {
      return platform::WasapiStreamIoResult::failure({
          {"render_script_exhausted", "No scripted render step remains."},
      });
    }

    auto step = std::move(render_steps_.front());
    render_steps_.pop_front();
    std::uint32_t committed_frames = 0;
    if (step.status == platform::WasapiStreamIoStatus::Completed) {
      RenderSubmission submission;
      submission.frames = std::min<std::uint32_t>(
          {step.writable_frames, static_cast<std::uint32_t>(source.frames()), frames});
      committed_frames = submission.frames;
      submission.samples.resize(source.channels());
      for (std::size_t channel = 0; channel < source.channels(); ++channel) {
        const auto source_samples = source.channel(channel);
        submission.samples[channel].assign(source_samples.begin(),
                                           source_samples.begin() + submission.frames);
      }
      render_submissions_.push_back(std::move(submission));
    }
    return make_result(committed_frames, step.status, false, false, false,
                       std::move(step.errors));
  }

  [[nodiscard]] platform::WasapiStreamIoResult capture_once(
      realtime::AudioBuffer& destination,
      std::uint32_t) noexcept override {
    if (io_call_log_ != nullptr) {
      io_call_log_->push_back(platform::WasapiStreamDirection::Capture);
    }
    if (capture_steps_.empty()) {
      return platform::WasapiStreamIoResult::failure({
          {"capture_script_exhausted", "No scripted capture step remains."},
      });
    }

    auto step = std::move(capture_steps_.front());
    capture_steps_.pop_front();
    if (step.status == platform::WasapiStreamIoStatus::Completed && !step.silent) {
      const auto channels = std::min(destination.channels(), step.samples.size());
      const auto frames = std::min<std::size_t>(destination.frames(), step.frames);
      for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto copied_frames = std::min(frames, step.samples[channel].size());
        std::copy_n(step.samples[channel].begin(), copied_frames,
                    destination.channel(channel).begin());
      }
    }
    return make_result(step.frames, step.status, step.silent,
                       step.data_discontinuity, step.timestamp_error,
                       std::move(step.errors));
  }

  void request_stop() noexcept override { ++request_stop_calls_; }

  [[nodiscard]] const platform::WasapiStreamProbe& probe() const noexcept override {
    return probe_;
  }

  [[nodiscard]] std::size_t remaining_capture_steps() const noexcept {
    return capture_steps_.size();
  }
  [[nodiscard]] std::size_t remaining_render_steps() const noexcept {
    return render_steps_.size();
  }
  [[nodiscard]] const std::vector<RenderSubmission>& render_submissions() const noexcept {
    return render_submissions_;
  }
  [[nodiscard]] std::size_t start_calls() const noexcept { return start_calls_; }
  [[nodiscard]] std::size_t stop_calls() const noexcept { return stop_calls_; }
  [[nodiscard]] std::size_t request_stop_calls() const noexcept {
    return request_stop_calls_;
  }

 private:
  static platform::WasapiStreamIoResult make_result(
      std::uint32_t frames,
      platform::WasapiStreamIoStatus status,
      bool silent,
      bool data_discontinuity,
      bool timestamp_error,
      std::vector<platform::WasapiStreamError> errors) {
    switch (status) {
      case platform::WasapiStreamIoStatus::Completed:
      case platform::WasapiStreamIoStatus::Idle:
        return silent ? platform::WasapiStreamIoResult::success_silent(
                            frames, data_discontinuity, timestamp_error)
                      : platform::WasapiStreamIoResult::success(
                            frames, data_discontinuity, timestamp_error);
      case platform::WasapiStreamIoStatus::TimedOut:
        return platform::WasapiStreamIoResult::timeout();
      case platform::WasapiStreamIoStatus::Cancelled:
        return platform::WasapiStreamIoResult::cancellation();
      case platform::WasapiStreamIoStatus::Failed:
        return platform::WasapiStreamIoResult::failure(std::move(errors));
    }
    return platform::WasapiStreamIoResult::failure({
        {"invalid_script_status", "The scripted I/O status is invalid."},
    });
  }

  platform::WasapiStreamProbe probe_;
  platform::WasapiStreamResult start_result_ = platform::WasapiStreamResult::success();
  platform::WasapiStreamResult stop_result_ = platform::WasapiStreamResult::success();
  std::deque<CaptureStep> capture_steps_;
  std::deque<RenderStep> render_steps_;
  std::vector<RenderSubmission> render_submissions_;
  std::vector<platform::WasapiStreamDirection>* io_call_log_ = nullptr;
  std::size_t start_calls_ = 0;
  std::size_t stop_calls_ = 0;
  std::size_t request_stop_calls_ = 0;
};

}  // namespace sar::tests
