#pragma once

#include <cassert>
#include <cstddef>
#include <initializer_list>

namespace sar::tests::fifo_runner {

enum class EventKind { capture, render, idle, cancel };

struct Event {
  EventKind kind;
  std::size_t frames = 0;
};

struct Snapshot {
  std::size_t capture_offered = 0;
  std::size_t capture_accepted = 0;
  std::size_t capture_dropped = 0;
  std::size_t graph_input_frames = 0;
  std::size_t graph_blocks = 0;
  std::size_t render_produced = 0;
  std::size_t render_requested = 0;
  std::size_t render_committed = 0;
  std::size_t render_underflow = 0;
  std::size_t render_dropped = 0;
  std::size_t capture_backlog = 0;
  std::size_t render_backlog = 0;
  std::size_t idle_events = 0;
  bool cancelled = false;
};

// Deterministic contract model for the future FIFO-backed graph runner. It
// deliberately models frame accounting, not production implementation detail.
class Fixture {
 public:
  Fixture(std::size_t quantum,
          std::size_t capture_capacity,
          std::size_t render_capacity)
      : quantum_(quantum),
        capture_capacity_(capture_capacity),
        render_capacity_(render_capacity) {
    assert(quantum > 0);
    assert(capture_capacity >= quantum);
    assert(render_capacity >= quantum);
  }

  [[nodiscard]] Snapshot run(std::initializer_list<Event> events) const {
    Snapshot result;
    for (const auto& event : events) {
      if (result.cancelled) {
        break;
      }

      switch (event.kind) {
        case EventKind::capture:
          capture(event.frames, result);
          process_ready_blocks(result);
          break;
        case EventKind::render:
          render(event.frames, result);
          break;
        case EventKind::idle:
          ++result.idle_events;
          break;
        case EventKind::cancel:
          result.cancelled = true;
          break;
      }
    }
    return result;
  }

 private:
  void capture(std::size_t frames, Snapshot& result) const {
    result.capture_offered += frames;
    const auto free_frames = capture_capacity_ - result.capture_backlog;
    const auto accepted = frames < free_frames ? frames : free_frames;
    result.capture_accepted += accepted;
    result.capture_dropped += frames - accepted;
    result.capture_backlog += accepted;
  }

  void process_ready_blocks(Snapshot& result) const {
    while (result.capture_backlog >= quantum_) {
      result.capture_backlog -= quantum_;
      result.graph_input_frames += quantum_;
      ++result.graph_blocks;
      result.render_produced += quantum_;

      const auto free_frames = render_capacity_ - result.render_backlog;
      const auto queued = quantum_ < free_frames ? quantum_ : free_frames;
      result.render_backlog += queued;
      result.render_dropped += quantum_ - queued;
    }
  }

  static void render(std::size_t frames, Snapshot& result) {
    result.render_requested += frames;
    const auto committed = frames < result.render_backlog ? frames : result.render_backlog;
    result.render_committed += committed;
    result.render_underflow += frames - committed;
    result.render_backlog -= committed;
  }

  std::size_t quantum_;
  std::size_t capture_capacity_;
  std::size_t render_capacity_;
};

inline void assert_conservation(const Snapshot& result) {
  assert(result.capture_offered == result.capture_accepted + result.capture_dropped);
  assert(result.capture_accepted == result.graph_input_frames + result.capture_backlog);
  assert(result.graph_input_frames == result.render_produced);
  assert(result.render_produced ==
         result.render_committed + result.render_backlog + result.render_dropped);
  assert(result.render_requested == result.render_committed + result.render_underflow);
}

}  // namespace sar::tests::fifo_runner
