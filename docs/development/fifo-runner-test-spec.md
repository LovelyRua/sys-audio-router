# FIFO Runner Test Specification

## Scope

This specification defines the observable contract for the planned FIFO-backed
WASAPI graph runner. Until its production API exists,
`tests/realtime/fifo_runner_test_fixture.h` is the executable reference model.
It is intentionally independent of Windows and does not prescribe runner
internals.

The graph quantum is four frames in every matrix case. Capture and render FIFO
capacities are eight frames unless the case states otherwise.

## Required Matrix

| Case | Events | Required outcome |
| --- | --- | --- |
| Capture 2+2 | capture 2, capture 2 | The first event leaves two capture frames queued. The second produces exactly one four-frame graph block and four queued render frames. |
| Render 1+1+2 | capture 4, render 1, render 1, render 2 | One graph block is drained over three render commits without duplication, underflow, or residual render backlog. |
| Idle backlog | capture 2, idle, idle | Idle events preserve the two-frame capture backlog and do not invoke the graph. |
| Overflow | capacity 4; capture 4, capture 4 | Both complete graph blocks are accounted for. The first remains queued for render and the second is reported as four dropped render frames. |
| Cancellation | capture 2, cancel, then more events | Cancellation is terminal for the run. Later capture/render events are not observed, and the existing two-frame backlog is unchanged. |
| Conservation | every case | All frame accounting identities below hold at the end of the event sequence. |

The first production test should additionally verify sample ordering with
monotonic per-channel frame IDs. The model currently focuses on scheduling and
accounting because no production runner FIFO API is available to receive or
inspect those samples.

## Conservation Assertions

Every runner result must expose enough counters or test-only observations to
prove these identities:

```text
capture offered = capture accepted + capture dropped
capture accepted = graph input + capture backlog
graph input = render produced
render produced = render committed + render backlog + render dropped
render requested = render committed + render underflow
```

Counters are cumulative within one run. Backlog values are end-of-run FIFO
depths. A frame may appear in exactly one term on each side of an identity.

## Production Fixture Contract

When the runner API lands, replace the model invocation with an adapter that:

1. Supplies capture packets and render requests in the listed event order.
2. Uses a passthrough graph with a four-frame quantum.
3. Records graph calls and render commits without allocation in the realtime
   callback.
4. Maps native runner statistics to `Snapshot`, or provides equivalent fields.
5. Treats cancellation as a terminal wakeup, not an idle or timeout cycle.
6. Runs the same matrix and `assert_conservation` checks on Windows and in the
   synthetic smoke suite.

Overflow policy may later change from drop-newest render output, but it must be
explicit and preserve the conservation identities. Update the overflow row and
fixture together if the production policy differs.
