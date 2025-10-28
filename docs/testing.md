# Testing

The project uses CMake to build self-contained test executables. The
recommended workflow is:

1. Configure the build directory:
   ```sh
   cmake -S . -B build
   ```
2. Build all test targets:
   ```sh
   cmake --build build --target \
     config_schema_test \
     world_state_step_order_test \
     systems_behavior_test \
     job_ability_system_test \
     ui_view_test
   ```
3. Run the complete suite with CTest:
   ```sh
   ctest --test-dir build
   ```
   You can filter to a specific executable with `-R`, for example:
   ```sh
   ctest --test-dir build -R job_ability_system
   ```

`world_state_step_order_test` covers the system pipeline ordering, while
`systems_behavior_test` validates formation alignment timers, commander
morale transitions, and spawn pity weighting. `job_ability_system_test`
exercises cooldowns, rally toggles, and spawn-rate boosts in the job
ability system. `ui_view_test` snapshots the HUD drawing logic with fake
renderers to catch formatting or visibility regressions in the mission,
morale, job, and warning overlays.

## Telemetry capture and frame dumps

The runtime now defaults to a rotating JSONL telemetry log. Files are
written to `build/debug_dumps/` (or the directory passed via
`--telemetry-dir`) and automatically rotate at 10&nbsp;MB, pruning to the
newest eight files. Rotation events are emitted back through the sink as
`telemetry.rotation` records to simplify monitoring.

Automated tests and debug tools can direct telemetry output by calling
`GameApplication::setTelemetryOutputDirectory` before `registerCoreServices`
runs, or by launching the executable with the `--telemetry-dir` CLI flag.

Requesting a frame capture via `ServiceLocator::instance().telemetrySink()->requestFrameCapture()`
captures the next five frames into the same directory as
`frame_capture_XXXX_YY.json`. Each capture file contains commander, ally,
enemy, and wall snapshots and triggers telemetry events for both success
(`world.frame_capture.saved`) and failure (`world.frame_capture.error`).

## Frame-budget telemetry

`assets/game.json` now exposes a `performance` block that defines CPU, GPU,
input, and HUD budgets in milliseconds, along with a shared tolerance. The
runtime samples each stage every frame; if a sample exceeds `budget +
tolerance`, the battle scene:

* records `battle.performance.budget_exceeded` telemetry with stage,
  threshold, and tolerance metadata,
* requests a frame capture (throttled to at most once per second), and
* displays a red HUD banner for the configured telemetry duration.

The standalone `performance_budget_monitor_test` executable exercises the
budget evaluator with forced timings to ensure the frame-capture request is
issued when budgets are exceeded.
