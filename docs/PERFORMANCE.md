# macOS frame-pacing validation

Tested on October 26, 2026 with Metal and a 120 Hz display. Native ARM64 game
code remains at its original simulation rate; this is not 120 FPS interpolation.

## Changes

- Default VSync on, precise frame timing on, early-presentation smoothing on.
- Use command-buffer presentation and a stable minimum duration on compatible
  60/120 Hz display cadences. Automatic mode leaves 0.5 ms tolerance before the
  60 Hz interval; the window server displays frames at the matching vblank.
- Skip absent Metal drawables instead of constructing an invalid render pass.
- Remove the second controller-device poll from the render-thread options hook.

## Observations

Apple's `metalperftrace` was filtered to the isolated game test processes.
A VSync-only reference still showed recurring 8.33 ms and 25 ms intervals on
120 Hz output, despite a roughly 60 FPS average. With minimum-duration
presentation enabled, most active one-second windows were 16.667 ms with
less than 0.003 ms reported standard deviation. The shortest observed interval
was 16.662 ms; the recurring 8.33 ms intervals were eliminated in this run.

There were still isolated outliers up to approximately 150 ms around startup,
loading, and automated captures. Do not interpret the regular cadence as proof
that all gameplay hitches are solved. The reference and final tests included
different startup content and durations, so they are not a controlled full-game
percentage-speedup benchmark. Testing both cities and Crazy Box is outstanding.

The final run reached the animated title screen at approximately 59.94 game
FPS and full emulation speed. Its log records the actual connected DualShock 4
and the 120 Hz / 16.167 ms minimum-duration selection.

## Local evidence (not tracked)

- `build/logs/pacing-overview.json`: VSync-only reference, process 75551.
- `build/logs/final-pacing-overview.json`: minimum-duration run, process 78480.
- `build/logs/final-pacing-metal.log`: runtime configuration and shutdown counters.
- `build/logs/final-pacing-result.json`: periodic runtime status.
- `build/final-pacing-automation/screen-4.png`: animated title-screen capture.
- Raw Metal traces: `build/metal-traces/` and `build/metal-traces-final/`.

Use the normal run command with the same `--user-dir` as your saved settings.
If testing on an external display with a different refresh rate, restart the
runner there so automatic presentation cadence is recalculated.
