# Team Workstreams

This document defines ownership for the three active engineering worktrees. The
goal is to let engineers work in parallel without editing the same implementation
files. Ownership is about who lands changes in an area, not who may review or
suggest changes.

## Engineer A: Windows Backend And Integration

Engineer A owns:

- `core/platform/windows_wasapi_stream*`
- `core/platform/windows_wasapi_graph_runner*`
- `core/platform/windows_wasapi_realtime_worker*`
- `core/platform/windows_wasapi_device_provider*`
- `core/platform/windows_realtime_thread*`
- Matching low-level Windows smoke tests.
- Windows device discovery, stream lifecycle, buffer pumping, graph runner,
  realtime worker, and MMCSS primitives.
- Cross-workstream integration, release readiness, and merges to `main`.
- Shared integration files listed below.

Current focus: stable device-selected endpoint APIs, loopback capture primitives,
and realtime worker correctness. High-level loop wrappers and measurement CLI
changes are handed to Engineer C after the backend API lands.

## Engineer B: Portable Engine And Control Plane

Engineer B owns:

- `core/realtime/**`
- `core/graph/**`
- `core/control/**`
- Portable files under `core/platform/` that model devices and virtual
  endpoints, except sample conversion.
- Matching smoke tests that do not start with `windows_` or `wasapi_measure_`.
- Graph execution, route matrix, snapshots, presets, control commands, and
  portable stress tests.

Current focus: realtime graph stress coverage, snapshot publication, route
matrix behavior, and control-plane correctness. Engineer B must not edit
`core/platform/windows_*`, WASAPI tests, or measurement tools.

## Engineer C: Diagnostics And Lab Tooling

Engineer C owns:

- `core/diagnostics/**`
- `core/platform/sample_converter.*`
- `tools/**`
- `scripts/**`
- `core/platform/windows_wasapi_render_loop.*`
- `core/platform/windows_wasapi_duplex_loop.*`
- `core/platform/windows_wasapi_runtime_summary.*`
- `core/platform/windows_wasapi_loop_preflight.*`
- `tests/realtime/wasapi_measure_*`
- Matching render/duplex/loop/preflight/runtime-summary smoke tests.
- Sample-converter, diagnostics, and test-infrastructure coverage.
- `docs/lab/**`

Current focus: high-level render/duplex/loopback wrappers,
measurement/reporting tools, soak-test automation, sample conversion
diagnostics, and reliable multi-user test scripts. Engineer C must not edit
WASAPI stream, graph runner, realtime worker, device provider, or MMCSS
implementations. When a wrapper or tool needs a new backend primitive, C
requests it from A and builds against it after it lands.

## Shared Integration Files

Only Engineer A edits these files during normal parallel work:

- `CMakeLists.txt`
- `AGENTS.md`
- `.github/**`
- `docs/roadmap.md`
- `docs/architecture/current-system.md`
- This workstream document

B or C may describe a required shared-file change in the PR body. A applies it
during integration. A may explicitly delegate a shared file for one named PR.

## Branch And Merge Rules

- Engineer A integrates from `system-audio-route-a` and is the only engineer who
  pushes `main`.
- Engineer B uses `codex/engineer-b-<topic>` from
  `system-audio-route-b`.
- Engineer C uses `codex/engineer-c-<topic>` from
  `system-audio-route-c`.
- B and C open PRs and do not merge them. A reviews, tests, rebases if needed,
  and merges.
- Each PR should stay inside one ownership area. A cross-owner change is split
  into ordered PRs unless A explicitly accepts an integration PR.
- Before starting a work item, fetch `origin`, rebase or branch from
  `origin/main`, and run `codegraph sync`.
- Before requesting review, run the smallest relevant local tests and the
  engineer-specific WinRM slot when Windows behavior is involved.

## Handoff Protocol

When work crosses a boundary, the current owner records a short handoff with:

1. The public API or behavior needed.
2. The owning engineer who should implement it.
3. The files the requester will change after the dependency lands.
4. The test that proves the handoff is complete.

Do not solve a handoff by editing the other owner's implementation files. This
keeps commits independently reviewable and prevents two branches from carrying
different versions of the same backend logic.

## Conflict Check

Before opening a PR, inspect its file list:

```bat
git diff --name-only origin/main...HEAD
```

If the list includes another engineer's area or a shared integration file,
remove that part from the PR or ask Engineer A to coordinate it before review.
