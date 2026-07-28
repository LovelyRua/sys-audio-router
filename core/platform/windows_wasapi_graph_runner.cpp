#include "core/platform/windows_wasapi_graph_runner.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sar::platform {

static_assert(std::atomic<double>::is_always_lock_free,
              "Duplex clock feed-forward must remain lock-free");

namespace {

std::vector<WasapiStreamError> validate_graph_shape(
    const graph::Graph& graph,
    const realtime::AudioBuffer& input,
    const realtime::AudioBuffer& output) {
  if (graph.node_count() <= 1) {
    return {};
  }

  const auto required_channels = std::max(input.channels(), output.channels());
  const auto required_frames = std::max(input.frames(), output.frames());
  if (graph.channels() >= required_channels && graph.frames() >= required_frames) {
    return {};
  }

  return {
      {
          "graph_buffer_too_small",
          "Graph scratch buffers must cover the runner input and output shapes.",
      },
  };
}

std::vector<WasapiStreamError> validate_graph_sample_rate(
    const graph::Graph& graph,
    const WasapiStreamIo* capture_stream,
    const WasapiStreamIo* render_stream,
    bool adapt_capture_rate = false) {
  if (!adapt_capture_rate && capture_stream != nullptr &&
      graph.sample_rate() != capture_stream->probe().mix_format.sample_rate) {
    return {
        {
            "graph_sample_rate_mismatch",
            "Graph sample rate must match the WASAPI capture stream sample rate.",
        },
    };
  }

  if (render_stream != nullptr &&
      graph.sample_rate() != render_stream->probe().mix_format.sample_rate) {
    return {
        {
            "graph_sample_rate_mismatch",
            "Graph sample rate must match the WASAPI render stream sample rate.",
        },
    };
  }

  return {};
}

std::size_t source_frames_for_output(std::size_t output_frames,
                                     double ratio) noexcept {
  return static_cast<std::size_t>(
      std::ceil(static_cast<double>(output_frames) / ratio));
}

void interleave_planar(const realtime::AudioBuffer& source,
                       std::size_t frames,
                       std::span<float> destination) noexcept {
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t channel = 0; channel < source.channels(); ++channel) {
      destination[frame * source.channels() + channel] = source.channel(channel)[frame];
    }
  }
}

void deinterleave_planar(std::span<const float> source,
                         std::size_t frames,
                         realtime::AudioBuffer& destination) noexcept {
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t channel = 0; channel < destination.channels(); ++channel) {
      destination.channel(channel)[frame] =
          source[frame * destination.channels() + channel];
    }
  }
}

bool has_sample_conversion_failure(
    const std::vector<WasapiStreamError>& errors) noexcept {
  for (const auto& error : errors) {
    if (error.code == "unsupported_sample_format" ||
        error.code == "sample_buffer_too_small" ||
        error.code == "sample_channel_mismatch" ||
        error.code == "sample_conversion_failed") {
      return true;
    }
  }
  return false;
}

void record_sample_conversion_failure(
    const std::vector<WasapiStreamError>& errors,
    std::uint64_t& counter) noexcept {
  if (has_sample_conversion_failure(errors)) {
    ++counter;
  }
}

}  // namespace

WasapiGraphRunnerResult WasapiGraphRunnerResult::success(WasapiGraphRunnerStats stats) {
  return {stats, {}};
}

WasapiGraphRunnerResult WasapiGraphRunnerResult::failure(
    std::vector<WasapiStreamError> errors) {
  return {{}, std::move(errors)};
}

bool WasapiGraphRunnerResult::ok() const noexcept {
  return errors_.empty();
}

const WasapiGraphRunnerStats& WasapiGraphRunnerResult::stats() const noexcept {
  return stats_;
}

const std::vector<WasapiStreamError>& WasapiGraphRunnerResult::errors() const noexcept {
  return errors_;
}

WasapiGraphRunnerResult::WasapiGraphRunnerResult(WasapiGraphRunnerStats stats,
                                                 std::vector<WasapiStreamError> errors)
    : stats_(stats), errors_(std::move(errors)) {}

WindowsWasapiGraphRunner::WindowsWasapiGraphRunner(WasapiStreamIo* capture_stream,
                                                   WasapiStreamIo* render_stream,
                                                   std::size_t channels,
                                                   std::size_t frames)
    : WindowsWasapiGraphRunner(capture_stream,
                               render_stream,
                               channels,
                               frames,
                               channels,
                               frames) {}

WindowsWasapiGraphRunner::WindowsWasapiGraphRunner(WasapiStreamIo* capture_stream,
                                                   WasapiStreamIo* render_stream,
                                                   std::size_t capture_channels,
                                                   std::size_t capture_frames,
                                                   std::size_t render_channels,
                                                   std::size_t render_frames)
    : capture_stream_(capture_stream),
      render_stream_(render_stream),
      native_capture_stream_(dynamic_cast<WindowsWasapiStream*>(capture_stream)),
      native_render_stream_(dynamic_cast<WindowsWasapiStream*>(render_stream)),
      input_(capture_channels, capture_frames),
      output_(render_channels, render_frames) {}

WindowsWasapiGraphRunner::BufferedPath::BufferedPath(
    std::size_t channels,
    std::size_t packet_frames,
    std::size_t fifo_frames)
    : packet(channels, packet_frames), fifo(channels, fifo_frames) {}

WindowsWasapiGraphRunner::CaptureRateAdapter::CaptureRateAdapter(
    std::size_t channels,
    std::size_t graph_frames,
    std::size_t fifo_frames,
    std::uint32_t capture_sample_rate,
    std::uint32_t graph_sample_rate)
    : nominal_ratio(static_cast<double>(graph_sample_rate) /
                    static_cast<double>(capture_sample_rate)),
      controller({.target_fill_frames =
                      source_frames_for_output(graph_frames, nominal_ratio),
                  .maximum_correction_ppm = 2500.0,
                  .maximum_slew_ppm_per_second = 250.0}),
      source_planar(channels, fifo_frames),
      source_interleaved(channels * fifo_frames, 0.0F),
      output_interleaved(channels * graph_frames, 0.0F),
      target_fill_frames(controller.config().target_fill_frames),
      ratio(nominal_ratio) {
  if (resampler.initialize(channels) !=
      realtime::AdaptiveResamplerStatus::success) {
    throw std::runtime_error("Failed to initialize capture adaptive resampler");
  }
}

WindowsWasapiGraphRunner::WindowsWasapiGraphRunner(
    WasapiStreamIo* capture_stream,
    WasapiStreamIo* render_stream,
    std::size_t input_channels,
    std::size_t output_channels,
    std::size_t graph_block_frames,
    std::size_t capture_packet_capacity_frames,
    std::size_t render_packet_capacity_frames,
    std::size_t fifo_capacity_frames,
    bool prime_render_silence,
    bool adapt_capture_rate,
    RealtimeAudioSource* external_input)
    : capture_stream_(capture_stream),
      render_stream_(render_stream),
      native_capture_stream_(dynamic_cast<WindowsWasapiStream*>(capture_stream)),
      native_render_stream_(dynamic_cast<WindowsWasapiStream*>(render_stream)),
      input_(input_channels, graph_block_frames),
      output_(output_channels, graph_block_frames),
      graph_block_frames_(graph_block_frames),
      render_master_(
          prime_render_silence ||
          (capture_stream == nullptr && render_stream != nullptr &&
           external_input != nullptr)),
      external_input_(external_input) {
  if (fifo_capacity_frames < graph_block_frames ||
      (capture_stream != nullptr && capture_packet_capacity_frames == 0) ||
      (render_stream != nullptr && render_packet_capacity_frames == 0)) {
    throw std::invalid_argument("Invalid buffered WASAPI graph runner capacity");
  }
  if (adapt_capture_rate) {
    if (capture_stream == nullptr || render_stream == nullptr ||
        !prime_render_silence ||
        capture_stream->probe().mix_format.sample_rate == 0 ||
        render_stream->probe().mix_format.sample_rate == 0) {
      throw std::invalid_argument("Invalid adaptive capture stream configuration");
    }
    const auto nominal_ratio =
        static_cast<double>(render_stream->probe().mix_format.sample_rate) /
        capture_stream->probe().mix_format.sample_rate;
    const auto nominal_capture_block =
        source_frames_for_output(graph_block_frames, nominal_ratio);
    if (nominal_capture_block > fifo_capacity_frames ||
        capture_packet_capacity_frames >
            fifo_capacity_frames - nominal_capture_block) {
      throw std::invalid_argument(
          "Adaptive capture FIFO must hold one graph block and capture packet");
    }
  }
  if (capture_stream != nullptr) {
    capture_path_.emplace(input_channels, capture_packet_capacity_frames,
                          fifo_capacity_frames);
    if (adapt_capture_rate) {
      capture_rate_adapter_.emplace(input_channels, graph_block_frames,
                                    fifo_capacity_frames,
                                    capture_stream->probe().mix_format.sample_rate,
                                    render_stream->probe().mix_format.sample_rate);
    }
  }
  if (render_stream != nullptr) {
    render_path_.emplace(output_channels, render_packet_capacity_frames,
                         fifo_capacity_frames);
    if (prime_render_silence) {
      static_cast<void>(render_path_->fifo.push(output_, graph_block_frames_));
    }
  }
}

realtime::AudioBuffer& WindowsWasapiGraphRunner::input_buffer() noexcept {
  return input_;
}

const realtime::AudioBuffer& WindowsWasapiGraphRunner::input_buffer() const noexcept {
  return input_;
}

realtime::AudioBuffer& WindowsWasapiGraphRunner::output_buffer() noexcept {
  return output_;
}

const realtime::AudioBuffer& WindowsWasapiGraphRunner::output_buffer() const noexcept {
  return output_;
}

WasapiGraphRunnerResult WindowsWasapiGraphRunner::start_streams() noexcept {
  if (capture_path_) {
    capture_path_->fifo.clear();
  }
  if (render_path_) {
    render_path_->fifo.clear();
    if (render_master_) {
      output_.clear();
      static_cast<void>(render_path_->fifo.push(output_, graph_block_frames_));
    }
  }
  if (capture_rate_adapter_) {
    capture_rate_adapter_->resampler.reset();
    capture_rate_adapter_->controller.reset();
    capture_rate_adapter_->output_frames_ready = 0;
    capture_rate_adapter_->ratio = capture_rate_adapter_->nominal_ratio;
    capture_rate_adapter_->ratio_set_for_block = false;
    capture_rate_adapter_->primed = false;
    capture_rate_adapter_->recovery_active = false;
    capture_rate_adapter_->recovery_preroll_pending = false;
    capture_rate_adapter_->ever_produced_graph_block = false;
  }

  if (capture_stream_ != nullptr) {
    auto result = capture_stream_->start();
    if (!result.ok()) {
      return WasapiGraphRunnerResult::failure(result.errors());
    }
  }

  if (render_stream_ != nullptr) {
    auto result = render_stream_->start();
    if (!result.ok()) {
      auto errors = result.errors();
      if (capture_stream_ != nullptr) {
        auto rollback_result = capture_stream_->stop();
        if (!rollback_result.ok()) {
          errors.insert(errors.end(), rollback_result.errors().begin(),
                        rollback_result.errors().end());
        }
      }
      return WasapiGraphRunnerResult::failure(std::move(errors));
    }
  }

  return WasapiGraphRunnerResult::success({});
}

WasapiGraphRunnerResult WindowsWasapiGraphRunner::stop_streams() noexcept {
  std::vector<WasapiStreamError> errors;

  if (render_stream_ != nullptr) {
    auto result = render_stream_->stop();
    if (!result.ok()) {
      errors.insert(errors.end(), result.errors().begin(), result.errors().end());
    }
  }

  if (capture_stream_ != nullptr) {
    auto result = capture_stream_->stop();
    if (!result.ok()) {
      errors.insert(errors.end(), result.errors().begin(), result.errors().end());
    }
  }

  if (!errors.empty()) {
    return WasapiGraphRunnerResult::failure(std::move(errors));
  }

  return WasapiGraphRunnerResult::success({});
}

void WindowsWasapiGraphRunner::request_stop() noexcept {
  if (render_stream_ != nullptr) {
    render_stream_->request_stop();
  }
  if (capture_stream_ != nullptr) {
    capture_stream_->request_stop();
  }
}

void WindowsWasapiGraphRunner::set_capture_clock_feed_forward_ppm(
    double correction_ppm) noexcept {
  constexpr double kMaximumCorrectionPpm = 2500.0;
  const auto bounded = std::isfinite(correction_ppm)
                           ? std::clamp(correction_ppm,
                                        -kMaximumCorrectionPpm,
                                        kMaximumCorrectionPpm)
                           : 0.0;
  capture_clock_feed_forward_ppm_.store(bounded, std::memory_order_relaxed);
}

double WindowsWasapiGraphRunner::capture_clock_feed_forward_ppm() const noexcept {
  return capture_clock_feed_forward_ppm_.load(std::memory_order_relaxed);
}

WasapiGraphRunnerResult WindowsWasapiGraphRunner::process_once(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    std::uint32_t timeout_ms) noexcept {
  if (graph_block_frames_ != 0) {
    return process_buffered_once(graph, diagnostics, timeout_ms);
  }

  WasapiGraphRunnerStats stats;

  auto sample_rate_errors =
      validate_graph_sample_rate(graph, capture_stream_, render_stream_);
  if (!sample_rate_errors.empty()) {
    return WasapiGraphRunnerResult::failure(std::move(sample_rate_errors));
  }

  if (capture_stream_ != nullptr) {
    input_.clear();
    auto capture_result = capture_stream_->capture_once(input_, timeout_ms);
    if (!capture_result.ok()) {
      record_sample_conversion_failure(
          capture_result.errors(),
          diagnostics.sample_conversion_import_failures);
      return WasapiGraphRunnerResult::failure(capture_result.errors());
    }
    if (capture_result.cancelled()) {
      stats.cancelled = true;
      return WasapiGraphRunnerResult::success(stats);
    }
    stats.captured_frames = capture_result.frames();
    stats.capture_silent = capture_result.silent();
    stats.capture_data_discontinuity = capture_result.data_discontinuity();
    stats.capture_timestamp_error = capture_result.timestamp_error();
    if (stats.capture_data_discontinuity) {
      diagnostics.xrun_count += 1;
    }
    if (stats.capture_silent) {
      stats.capture_silent_frames = stats.captured_frames;
    }
    stats.capture_partial =
        stats.captured_frames > 0 && stats.captured_frames < input_.frames();
    if (stats.capture_partial) {
      stats.capture_partial_frames =
          static_cast<std::uint32_t>(input_.frames() - stats.captured_frames);
    }
    if (stats.captured_frames == 0) {
      stats.capture_stream_idle = true;
      stats.capture_wait_timed_out = capture_result.timed_out();
      return WasapiGraphRunnerResult::success(stats);
    }
  } else if (external_input_ != nullptr) {
    static_cast<void>(external_input_->read(input_));
  }

  auto shape_errors = validate_graph_shape(graph, input_, output_);
  if (!shape_errors.empty()) {
    return WasapiGraphRunnerResult::failure(std::move(shape_errors));
  }

  output_.clear();
  graph.process(input_, output_, diagnostics);
  stats.graph_processed = true;

  if (render_stream_ != nullptr) {
    auto render_result = render_stream_->render_once(
        output_, static_cast<std::uint32_t>(output_.frames()), timeout_ms);
    if (!render_result.ok()) {
      record_sample_conversion_failure(
          render_result.errors(),
          diagnostics.sample_conversion_export_failures);
      return WasapiGraphRunnerResult::failure(render_result.errors());
    }
    if (render_result.cancelled()) {
      stats.cancelled = true;
      return WasapiGraphRunnerResult::success(stats);
    }
    stats.rendered_frames = render_result.frames();
    stats.render_stream_idle = stats.rendered_frames == 0;
    stats.render_wait_timed_out = render_result.timed_out();
    stats.render_partial =
        stats.rendered_frames > 0 && stats.rendered_frames < output_.frames();
    if (stats.render_partial) {
      stats.render_partial_frames =
          static_cast<std::uint32_t>(output_.frames() - stats.rendered_frames);
    }
  }

  return WasapiGraphRunnerResult::success(stats);
}

WasapiGraphRunnerResult WindowsWasapiGraphRunner::process_buffered_once(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    std::uint32_t timeout_ms) noexcept {
  WasapiGraphRunnerStats stats;
  stats.capture_rate_adapter_active = capture_rate_adapter_.has_value();
  if (capture_rate_adapter_) {
    stats.capture_clock_feed_forward_ppm =
        capture_clock_feed_forward_ppm();
    stats.capture_fifo_correction_ppm =
        capture_rate_adapter_->controller.correction_ppm();
    stats.capture_rate_correction_ppm = std::clamp(
        stats.capture_clock_feed_forward_ppm +
            stats.capture_fifo_correction_ppm,
        -2500.0,
        2500.0);
    stats.capture_resampler_ratio = capture_rate_adapter_->ratio;
    stats.capture_rate_adapter_recovering =
        capture_rate_adapter_->recovery_active;
  }
  auto sample_rate_errors =
      validate_graph_sample_rate(graph, capture_stream_, render_stream_,
                                 capture_rate_adapter_.has_value());
  if (!sample_rate_errors.empty()) {
    return WasapiGraphRunnerResult::failure(std::move(sample_rate_errors));
  }

  bool coordinated_duplex_wait = false;
  if (render_master_ && native_capture_stream_ != nullptr &&
      native_render_stream_ != nullptr) {
    const auto wait_status = wait_for_wasapi_duplex_events(
        *native_capture_stream_, *native_render_stream_, timeout_ms);
    if (wait_status == WasapiDuplexEventWaitStatus::Cancelled) {
      stats.cancelled = true;
      return WasapiGraphRunnerResult::success(stats);
    }
    if (wait_status == WasapiDuplexEventWaitStatus::Failed) {
      return WasapiGraphRunnerResult::failure({{
          "wasapi_duplex_event_wait_failed",
          "WASAPI duplex event wait failed.",
      }});
    }
    coordinated_duplex_wait =
        wait_status != WasapiDuplexEventWaitStatus::Unavailable;
    stats.duplex_event_wait_timed_out =
        wait_status == WasapiDuplexEventWaitStatus::TimedOut;
  }
  const auto render_timeout_ms = coordinated_duplex_wait ? 0U : timeout_ms;

  const bool render_queued_at_cycle_entry =
      render_master_ && render_stream_ != nullptr &&
      render_path_->fifo.available_frames() > 0;
  auto submit_queued_render = [&]() -> std::optional<WasapiGraphRunnerResult> {
    render_path_->packet.clear();
    const auto staged = render_path_->fifo.peek(
        render_path_->packet, render_path_->packet.frames());
    auto render_result = render_stream_->render_once(
        render_path_->packet, static_cast<std::uint32_t>(staged), render_timeout_ms);
    if (!render_result.ok()) {
      record_sample_conversion_failure(
          render_result.errors(),
          diagnostics.sample_conversion_export_failures);
      return WasapiGraphRunnerResult::failure(render_result.errors());
    }
    if (render_result.cancelled()) {
      diagnostics.capture_fifo_fill_frames =
          capture_path_ ? capture_path_->fifo.available_frames() : 0;
      diagnostics.render_fifo_fill_frames = render_path_->fifo.available_frames();
      stats.cancelled = true;
      return WasapiGraphRunnerResult::success(stats);
    }
    if (render_result.frames() > staged) {
      return WasapiGraphRunnerResult::failure({{
          "render_committed_too_many_frames",
          "WASAPI render stream committed more frames than were staged.",
      }});
    }
    stats.rendered_frames = static_cast<std::uint32_t>(
        render_path_->fifo.consume(render_result.frames()));
    stats.render_stream_idle = stats.rendered_frames == 0;
    stats.render_wait_timed_out =
        !coordinated_duplex_wait && render_result.timed_out();
    stats.render_partial =
        stats.rendered_frames > 0 && stats.rendered_frames < staged;
    stats.render_partial_frames =
        stats.render_partial ? static_cast<std::uint32_t>(staged - stats.rendered_frames)
                             : 0;
    return std::nullopt;
  };

  if (render_queued_at_cycle_entry) {
    if (auto terminal_result = submit_queued_render()) {
      return std::move(*terminal_result);
    }
  }

  if (capture_stream_ != nullptr) {
    constexpr std::size_t kMaximumCapturePacketsPerCycle = 8;
    const auto packet_limit = render_master_ ? kMaximumCapturePacketsPerCycle : 1;
    for (std::size_t packet_index = 0; packet_index < packet_limit; ++packet_index) {
      if (render_master_ &&
          capture_path_->fifo.free_frames() < capture_path_->packet.frames()) {
        break;
      }
      capture_path_->packet.clear();
      const auto capture_timeout_ms = render_master_ ? 0 : timeout_ms;
      auto capture_result = capture_stream_->capture_once(
          capture_path_->packet, capture_timeout_ms);
      if (!capture_result.ok()) {
        record_sample_conversion_failure(
            capture_result.errors(),
            diagnostics.sample_conversion_import_failures);
        return WasapiGraphRunnerResult::failure(capture_result.errors());
      }
      if (capture_result.cancelled()) {
        stats.cancelled = true;
        return WasapiGraphRunnerResult::success(stats);
      }
      if (capture_result.frames() == 0) {
        stats.capture_wait_timed_out =
            !render_master_ && capture_result.timed_out();
        break;
      }

      stats.captured_frames += capture_result.frames();
      stats.capture_silent = stats.capture_silent || capture_result.silent();
      stats.capture_data_discontinuity =
          stats.capture_data_discontinuity || capture_result.data_discontinuity();
      stats.capture_timestamp_error =
          stats.capture_timestamp_error || capture_result.timestamp_error();
      if (capture_result.silent()) {
        stats.capture_silent_frames += capture_result.frames();
      }
      if (capture_result.data_discontinuity()) {
        ++diagnostics.xrun_count;
        if (capture_rate_adapter_) {
          capture_path_->fifo.clear();
          capture_rate_adapter_->resampler.reset();
          capture_rate_adapter_->output_frames_ready = 0;
          capture_rate_adapter_->ratio =
              capture_rate_adapter_->nominal_ratio;
          capture_rate_adapter_->ratio_set_for_block = false;
          capture_rate_adapter_->primed = false;
          capture_rate_adapter_->recovery_active = true;
          capture_rate_adapter_->recovery_preroll_pending = true;
          stats.capture_rate_adapter_reset = true;
          stats.capture_rate_adapter_recovering = true;
        }
      }

      const auto queued = capture_path_->fifo.push(
          capture_path_->packet, capture_result.frames());
      if (queued < capture_result.frames()) {
        ++diagnostics.capture_fifo_overflow_cycles;
        diagnostics.capture_fifo_overflow_frames +=
            capture_result.frames() - queued;
        ++diagnostics.xrun_count;
      }
    }
    stats.capture_stream_idle = stats.captured_frames == 0;
  }

  diagnostics.capture_fifo_fill_frames =
      capture_path_ ? capture_path_->fifo.available_frames() : 0;
  diagnostics.render_fifo_fill_frames =
      render_path_ ? render_path_->fifo.available_frames() : 0;

  constexpr std::size_t kMaximumGraphBlocksPerCycle = 8;
  const bool fill_render_packet = render_master_ || external_input_ != nullptr;
  const auto graph_limit =
      fill_render_packet ? kMaximumGraphBlocksPerCycle : std::size_t{1};
  const auto desired_render_fill = render_path_
      ? std::min(render_path_->fifo.capacity_frames(),
                 render_path_->packet.frames() +
                     (render_master_ ? graph_block_frames_ : std::size_t{0}))
      : std::size_t{0};
  for (std::size_t block_index = 0; block_index < graph_limit; ++block_index) {
    const bool render_has_space =
        !render_path_ ||
        (render_path_->fifo.free_frames() >= graph_block_frames_ &&
         (!fill_render_packet ||
          render_path_->fifo.available_frames() < desired_render_fill));
    if (!render_has_space) {
      break;
    }
    if (capture_path_) {
      if (capture_rate_adapter_) {
        auto& adapter = *capture_rate_adapter_;
        if (!adapter.primed) {
          if (capture_path_->fifo.available_frames() < adapter.target_fill_frames) {
            break;
          }
          adapter.primed = true;
        }

        if (adapter.recovery_preroll_pending) {
          const auto preroll_capacity = adapter.source_planar.frames();
          const auto nominal_preroll_frames = source_frames_for_output(
              graph_block_frames_, adapter.nominal_ratio);
          const auto extra_preroll_frames =
              preroll_capacity > nominal_preroll_frames
                  ? std::min(nominal_preroll_frames,
                             preroll_capacity - nominal_preroll_frames)
                  : std::size_t{0};
          const auto preroll_frames =
              nominal_preroll_frames + extra_preroll_frames;
          static_cast<void>(capture_path_->fifo.peek(adapter.source_planar, 1));
          for (std::size_t channel = 0; channel < input_.channels(); ++channel) {
            const auto edge_sample = adapter.source_planar.channel(channel)[0];
            std::fill_n(adapter.source_planar.channel(channel).begin(),
                        preroll_frames, edge_sample);
          }
          interleave_planar(adapter.source_planar, preroll_frames,
                            adapter.source_interleaved);

          std::size_t preroll_frames_used = 0;
          constexpr std::size_t kMaximumPrerollPasses = 8;
          for (std::size_t pass = 0;
               pass < kMaximumPrerollPasses &&
               preroll_frames_used < preroll_frames;
               ++pass) {
            const auto remaining_frames = preroll_frames - preroll_frames_used;
            const auto input_offset = preroll_frames_used * input_.channels();
            auto preroll_result = adapter.resampler.process(
                std::span<const float>(adapter.source_interleaved).subspan(
                    input_offset, remaining_frames * input_.channels()),
                static_cast<std::uint32_t>(remaining_frames),
                adapter.output_interleaved,
                static_cast<std::uint32_t>(graph_block_frames_),
                adapter.nominal_ratio);
            if (!preroll_result.ok()) {
              return WasapiGraphRunnerResult::failure({{
                  "capture_resampler_preroll_failed",
                  "Adaptive capture resampler recovery pre-roll failed.",
              }});
            }
            if (preroll_result.input_frames_used == 0 &&
                preroll_result.output_frames_generated == 0) {
              break;
            }
            preroll_frames_used += preroll_result.input_frames_used;
          }
          if (preroll_frames_used != preroll_frames) {
            return WasapiGraphRunnerResult::failure({{
                "capture_resampler_preroll_stalled",
                "Adaptive capture resampler recovery pre-roll made no progress.",
            }});
          }
          adapter.recovery_preroll_pending = false;
        }

        bool attempted_internal_drain = false;
        while (adapter.output_frames_ready < graph_block_frames_) {
          if (!adapter.ratio_set_for_block) {
            const auto elapsed_seconds =
                static_cast<double>(graph_block_frames_) / graph.sample_rate();
            const auto fifo_correction_ppm = adapter.controller.update(
                static_cast<double>(capture_path_->fifo.available_frames()),
                elapsed_seconds);
            const auto clock_feed_forward_ppm =
                capture_clock_feed_forward_ppm();
            const auto correction_ppm = std::clamp(
                clock_feed_forward_ppm + fifo_correction_ppm,
                -2500.0,
                2500.0);
            adapter.ratio =
                adapter.nominal_ratio / (1.0 + correction_ppm * 0.000001);
            adapter.ratio_set_for_block = true;
            stats.capture_rate_correction_ppm = correction_ppm;
            stats.capture_clock_feed_forward_ppm = clock_feed_forward_ppm;
            stats.capture_fifo_correction_ppm = fifo_correction_ppm;
            stats.capture_resampler_ratio = adapter.ratio;
          }

          std::size_t source_frames = 0;
          if (attempted_internal_drain) {
            const auto available = capture_path_->fifo.available_frames();
            if (available == 0) {
              break;
            }
            const auto remaining_output =
                graph_block_frames_ - adapter.output_frames_ready;
            const auto nominal_input = static_cast<std::size_t>(
                std::ceil(static_cast<double>(remaining_output) / adapter.ratio));
            const auto offer_limit =
                nominal_input < adapter.source_planar.frames()
                    ? nominal_input + 1
                    : adapter.source_planar.frames();
            source_frames = std::min(available, offer_limit);
            static_cast<void>(capture_path_->fifo.peek(adapter.source_planar,
                                                       source_frames));
            interleave_planar(adapter.source_planar, source_frames,
                              adapter.source_interleaved);
          }
          attempted_internal_drain = true;

          const auto output_offset =
              adapter.output_frames_ready * input_.channels();
          auto result = adapter.resampler.process(
              std::span<const float>(adapter.source_interleaved.data(),
                                     source_frames * input_.channels()),
              static_cast<std::uint32_t>(source_frames),
              std::span<float>(adapter.output_interleaved).subspan(output_offset),
              static_cast<std::uint32_t>(graph_block_frames_ -
                                         adapter.output_frames_ready),
              adapter.ratio);
          if (!result.ok()) {
            return WasapiGraphRunnerResult::failure({{
                "capture_resampler_failed",
                "Adaptive capture resampling failed in the buffered duplex path.",
            }});
          }
          static_cast<void>(
              capture_path_->fifo.consume(result.input_frames_used));
          stats.capture_resampler_input_frames += result.input_frames_used;
          stats.capture_resampler_output_frames +=
              result.output_frames_generated;
          adapter.output_frames_ready += result.output_frames_generated;
          if (result.input_frames_used == 0 &&
              result.output_frames_generated == 0) {
            if (source_frames == 0) {
              continue;
            }
            break;
          }
        }

        if (adapter.output_frames_ready < graph_block_frames_) {
          break;
        }
        deinterleave_planar(adapter.output_interleaved, graph_block_frames_, input_);
      } else {
        if (capture_path_->fifo.available_frames() < graph_block_frames_) {
          break;
        }
        static_cast<void>(capture_path_->fifo.pop(input_, graph_block_frames_));
      }
    } else if (external_input_ != nullptr && !external_input_->read(input_)) {
      break;
    }
    auto shape_errors = validate_graph_shape(graph, input_, output_);
    if (!shape_errors.empty()) {
      return WasapiGraphRunnerResult::failure(std::move(shape_errors));
    }
    output_.clear();
    graph.process(input_, output_, diagnostics);
    stats.graph_processed = true;
    if (capture_rate_adapter_) {
      capture_rate_adapter_->ever_produced_graph_block = true;
      capture_rate_adapter_->output_frames_ready = 0;
      capture_rate_adapter_->ratio_set_for_block = false;
      capture_rate_adapter_->recovery_active = false;
      stats.capture_rate_adapter_recovering = false;
    }
    if (render_path_) {
      const auto queued = render_path_->fifo.push(output_, graph_block_frames_);
      if (queued != graph_block_frames_) {
        ++diagnostics.render_fifo_overflow_cycles;
        diagnostics.render_fifo_overflow_frames += graph_block_frames_ - queued;
        ++diagnostics.xrun_count;
      }
    }
  }

  if (!render_queued_at_cycle_entry && render_stream_ != nullptr &&
      render_path_->fifo.available_frames() > 0) {
    if (auto terminal_result = submit_queued_render()) {
      return std::move(*terminal_result);
    }
  } else if (!render_queued_at_cycle_entry && render_stream_ != nullptr) {
    if (!render_master_) {
      stats.render_stream_idle = true;
      ++diagnostics.render_fifo_underflow_cycles;
      diagnostics.render_fifo_underflow_frames += graph_block_frames_;
      diagnostics.capture_fifo_fill_frames =
          capture_path_ ? capture_path_->fifo.available_frames() : 0;
      diagnostics.render_fifo_fill_frames = 0;
      return WasapiGraphRunnerResult::success(stats);
    }
    render_path_->packet.clear();
    auto render_result = render_stream_->render_once(
        render_path_->packet,
        static_cast<std::uint32_t>(render_path_->packet.frames()),
        render_timeout_ms);
    if (!render_result.ok()) {
      record_sample_conversion_failure(
          render_result.errors(),
          diagnostics.sample_conversion_export_failures);
      return WasapiGraphRunnerResult::failure(render_result.errors());
    }
    if (render_result.cancelled()) {
      stats.cancelled = true;
      return WasapiGraphRunnerResult::success(stats);
    }
    stats.rendered_frames = render_result.frames();
    stats.render_stream_idle = stats.rendered_frames == 0;
    stats.render_wait_timed_out =
        !coordinated_duplex_wait && render_result.timed_out();
    if (stats.rendered_frames > 0) {
      ++diagnostics.render_fifo_underflow_cycles;
      diagnostics.render_fifo_underflow_frames += stats.rendered_frames;
      if (capture_rate_adapter_ && capture_rate_adapter_->recovery_active) {
        stats.render_recovery_silence = true;
        stats.render_recovery_silence_frames = stats.rendered_frames;
      } else if (capture_rate_adapter_ &&
                 !capture_rate_adapter_->ever_produced_graph_block) {
        stats.render_startup_silence = true;
        stats.render_startup_silence_frames = stats.rendered_frames;
      } else if (capture_rate_adapter_) {
        stats.render_capture_starvation_silence = true;
        stats.render_capture_starvation_silence_frames = stats.rendered_frames;
      }
    }
  }

  diagnostics.capture_fifo_fill_frames =
      capture_path_ ? capture_path_->fifo.available_frames() : 0;
  diagnostics.render_fifo_fill_frames =
      render_path_ ? render_path_->fifo.available_frames() : 0;
  return WasapiGraphRunnerResult::success(stats);
}

}  // namespace sar::platform
