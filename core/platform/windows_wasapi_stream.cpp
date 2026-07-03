#include "core/platform/windows_wasapi_stream.h"

#include <utility>

namespace sar::platform {

namespace {

std::vector<WasapiStreamError> validate_probe(const WasapiStreamProbe& probe) {
  std::vector<WasapiStreamError> errors;

  if (probe.device_id.empty()) {
    errors.push_back({"empty_device_id", "WASAPI stream probe device ID must not be empty."});
  }
  if (probe.device_label.empty()) {
    errors.push_back({"empty_device_label", "WASAPI stream probe device label must not be empty."});
  }
  if (probe.mix_format.sample_rate == 0) {
    errors.push_back({"invalid_sample_rate", "WASAPI stream sample rate must be non-zero."});
  }
  if (probe.mix_format.channels == 0) {
    errors.push_back({"invalid_channel_count", "WASAPI stream channel count must be non-zero."});
  }
  if (probe.buffer_frames == 0) {
    errors.push_back({"invalid_buffer_frames", "WASAPI stream buffer size must be non-zero."});
  }
  if (probe.default_period_100ns == 0) {
    errors.push_back({"invalid_device_period", "WASAPI stream device period must be non-zero."});
  }

  return errors;
}

std::vector<WasapiStreamError> convert_probe_errors(
    const std::vector<WasapiStreamProbeError>& errors) {
  std::vector<WasapiStreamError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

}  // namespace

WasapiStreamResult WasapiStreamResult::success() {
  return WasapiStreamResult({});
}

WasapiStreamResult WasapiStreamResult::failure(std::vector<WasapiStreamError> errors) {
  return WasapiStreamResult(std::move(errors));
}

bool WasapiStreamResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<WasapiStreamError>& WasapiStreamResult::errors() const noexcept {
  return errors_;
}

WasapiStreamResult::WasapiStreamResult(std::vector<WasapiStreamError> errors)
    : errors_(std::move(errors)) {}

WasapiStreamResult WindowsWasapiStream::open(WasapiStreamProbe probe) {
  if (state_ != WasapiStreamState::Closed) {
    return WasapiStreamResult::failure({
        {"stream_already_open", "WASAPI stream shell is already open."},
    });
  }

  auto errors = validate_probe(probe);
  if (!errors.empty()) {
    return WasapiStreamResult::failure(std::move(errors));
  }

  probe_ = std::move(probe);
  state_ = WasapiStreamState::Open;
  return WasapiStreamResult::success();
}

WasapiStreamResult WindowsWasapiStream::start() {
  if (state_ == WasapiStreamState::Started) {
    return WasapiStreamResult::success();
  }
  if (state_ != WasapiStreamState::Open) {
    return WasapiStreamResult::failure({
        {"stream_not_open", "WASAPI stream shell must be open before start."},
    });
  }

  state_ = WasapiStreamState::Started;
  return WasapiStreamResult::success();
}

WasapiStreamResult WindowsWasapiStream::stop() {
  if (state_ == WasapiStreamState::Open) {
    return WasapiStreamResult::success();
  }
  if (state_ != WasapiStreamState::Started) {
    return WasapiStreamResult::failure({
        {"stream_not_started", "WASAPI stream shell is not started."},
    });
  }

  state_ = WasapiStreamState::Open;
  return WasapiStreamResult::success();
}

void WindowsWasapiStream::close() noexcept {
  state_ = WasapiStreamState::Closed;
  probe_ = {};
}

WasapiStreamState WindowsWasapiStream::state() const noexcept {
  return state_;
}

const WasapiStreamProbe& WindowsWasapiStream::probe() const noexcept {
  return probe_;
}

WasapiStreamDiagnostics WindowsWasapiStream::diagnostics() const noexcept {
  WasapiStreamDiagnostics diagnostics;
  diagnostics.state = state_;
  diagnostics.direction = probe_.direction;
  diagnostics.mix_format = probe_.mix_format;
  diagnostics.buffer_frames = probe_.buffer_frames;
  diagnostics.default_period_100ns = probe_.default_period_100ns;
  diagnostics.minimum_period_100ns = probe_.minimum_period_100ns;
  return diagnostics;
}

WasapiStreamOpenResult WasapiStreamOpenResult::success(WindowsWasapiStream stream) {
  return {std::move(stream), {}};
}

WasapiStreamOpenResult WasapiStreamOpenResult::failure(std::vector<WasapiStreamError> errors) {
  return {{}, std::move(errors)};
}

bool WasapiStreamOpenResult::ok() const noexcept {
  return errors_.empty();
}

WindowsWasapiStream& WasapiStreamOpenResult::stream() noexcept {
  return stream_;
}

const WindowsWasapiStream& WasapiStreamOpenResult::stream() const noexcept {
  return stream_;
}

WindowsWasapiStream WasapiStreamOpenResult::take_stream() noexcept {
  return std::move(stream_);
}

const std::vector<WasapiStreamError>& WasapiStreamOpenResult::errors() const noexcept {
  return errors_;
}

WasapiStreamOpenResult::WasapiStreamOpenResult(WindowsWasapiStream stream,
                                               std::vector<WasapiStreamError> errors)
    : stream_(std::move(stream)), errors_(std::move(errors)) {}

WasapiStreamOpenResult open_default_wasapi_stream_shell(WasapiStreamDirection direction) {
  auto probe_result = probe_default_wasapi_stream(direction);
  if (!probe_result.ok()) {
    return WasapiStreamOpenResult::failure(convert_probe_errors(probe_result.errors()));
  }

  WindowsWasapiStream stream;
  auto open_result = stream.open(probe_result.probe());
  if (!open_result.ok()) {
    return WasapiStreamOpenResult::failure(open_result.errors());
  }

  return WasapiStreamOpenResult::success(std::move(stream));
}

}  // namespace sar::platform
