# Realtime Transport Primitives

Realtime transport primitives move bounded control or audio-adjacent data between threads without blocking the audio path.

They are not a substitute for a full driver/service transport design, but they let the core prototype test the constraints early.

## Current Primitive

`SpscRingBuffer<T>` lives in:

- `core/realtime/spsc_ring_buffer.h`
- `tests/realtime/spsc_ring_buffer_smoke.cpp`

It is a fixed-capacity single-producer single-consumer queue.

## Contract

After construction, `push` and `pop` must:

- Avoid heap allocation.
- Avoid locks.
- Avoid blocking waits.
- Preserve FIFO order.
- Return failure instead of waiting when full or empty.

The current implementation uses:

- `std::vector<T>` for fixed storage allocated at construction time.
- Atomic read/write indices.
- One unused slot to distinguish full from empty.

## Intended Uses

Potential future uses:

- Control event queues into non-realtime preparation threads.
- Audio backend telemetry queues.
- Prototype ASIO-to-engine transport experiments.
- Test harnesses for xrun and overflow behavior.

It should not be assumed sufficient for final audio block transport until measured under target conditions.

## Future Work

- Add a `pop_into(T&)` API to avoid optional construction where needed.
- Add audio-block-specific queue experiments.
- Add stress tests with producer and consumer threads.
- Measure cache behavior and false sharing on target hardware.
- Add overflow/underflow diagnostics hooks.

