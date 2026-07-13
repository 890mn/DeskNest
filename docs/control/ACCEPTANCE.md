# Acceptance Rules: DeskNest

## Observable completion

A completed task names what changed, preserves its declared non-goals, and
provides evidence that is proportionate to its risk. Acceptance reports must
separate passed checks, checks not run, and remaining board-level risks.

## Control-plane-only work

Changes limited to `AGENTS.md`, `docs/control/`, `.tasks/`, or `.gitignore`
do not require a firmware build. They require a clean patch check, ignore-rule
verification, a review for accidental sensitive content, and confirmation that
unrelated working-tree files were preserved.

## Firmware work

Firmware-changing tasks build with:

```powershell
C:\Users\DF\.platformio\penv\Scripts\pio.exe run -e DeskNest
```

For logic changes, run the relevant host tests in `desknest_test` when the
changed logic is covered there. Do not run unrelated tests only for ceremony;
do not claim a host test proves K10 UI or hardware behavior.

## Stateful input changes

Gesture, sensor, timer, and other stateful-input changes require a minimum
edge/re-arm check in addition to the nominal trigger case:

- a held input must not emit repeated events after cooldown or detection-window
  expiry;
- the re-arm condition must be explicit and tested;
- a subsequent input must work after the re-arm condition is satisfied.

This may remain a small targeted test for a Level 0 task; it does not require a
full architecture plan when the boundary is unchanged.

## Git checkpoint metadata

Meaningful task commits should keep the subject concise while exposing the
HALF-Work level and workflow stage:

```text
[L0][Verify] fix(gesture): rearm after held tilt
```

Task commits also carry these Git trailers:

```text
HALF-Work-Level: L0
HALF-Work-Stage: Verify
HALF-Work-Task: held-tilt-rearm
```

`Observe`, `Plan`, `Implement`, `Verify`, and `Review` are the canonical stage
values. Do not create empty commits only to mark a stage; each checkpoint must
contain a meaningful reviewable change. A final verification or review commit
may add a short `HALF-Work-Evidence` trailer.

## UI and architecture boundaries

- UI tasks keep `src/ui_lvgl.cpp` as the only production renderer and consume
  `UiModel` rather than embedding business, state-machine, or sensor logic.
- A change to orientation, page ownership, data boundaries, or firmware build
  topology requires an explicit decision record and a board-oriented
  acceptance plan.
- `pc-simulator` results are supplemental. Board or firmware-path evidence has
  priority for product behavior.

## Fixed-screen layout tasks

- A fixed 240x320 layout names the usable content geometry and accounts for
  card heights, gaps, padding, header, and footer before implementation.
- The production renderer and its mockup keep the same information hierarchy;
  a mockup may use different rendering technology but must not silently promote
  a secondary module into the primary region.
- Static geometry checks and a successful firmware build are necessary
  evidence, but they do not claim board-level visual acceptance.

## Secrets and local configuration

No acceptance artifact may contain WiFi credentials, TokenNest endpoint
details, tokens, keys, or generated credential material. Local configuration and
generated credential files stay ignored; review staged and unstaged changes
before accepting a task.
