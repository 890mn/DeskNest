# Project Control: DeskNest

## Current goal

DeskNest is a persistent desktop assistant for the UNIHIKER K10. Its product
direction is a small, board-first display for AI usage, desktop environment,
and lightweight daily status, with interaction that remains practical on the
device.

## Runtime and build baseline

- Hardware/framework: UNIHIKER K10, Arduino, PlatformIO.
- Build root: repository root because `platformio.ini` declares `src_dir = .`.
- Primary firmware environment: `DeskNest`.
- Host test environment: `desknest_test`.
- Required PlatformIO executable:
  `C:\Users\DF\.platformio\penv\Scripts\pio.exe`.

## Main loop and UI data boundary

`dn_app_setup()` and `dn_app_loop()` are the firmware entry points. Runtime
state is assembled into `UiModel` by `src/ui_model.*`; `src/ui_lvgl.cpp`
consumes that model to render the only production UI. Business rules, gesture
state, and sensor acquisition do not belong in the renderer. `src/ui.cpp` is a
retired Canvas placeholder and is not a production fallback.

## Implemented baseline

The repository currently contains the LVGL screen/chrome and page renderer,
the `UiModel` construction boundary, firmware app-loop wiring, AI-usage and
desktop-status product flows, gesture/sleep behavior, and a local TokenNest
integration boundary. The tracked source is the authority for the exact
feature behavior.

## Git and worktree notes

- Stable control files under `docs/control/*.md` are versioned.
- Task checkpoints use a short subject such as
  `[L0][Verify] fix(gesture): rearm after held tilt` and Git trailers for
  `HALF-Work-Level`, `HALF-Work-Stage`, and `HALF-Work-Task`.
- `.tasks/`, `.codex/`, `*.local.md`, control private notes, and `WORKLOG.md`
  are local-only.
- `scripts/add_chinese_glyphs.py` is user-owned and currently untracked; leave
  it untouched unless explicitly requested.
- Credentials and generated secrets remain ignored and must never be copied
  into control documents.

## Known risks

- The host simulator is not the acceptance authority; visual or input changes
  can drift from the flashed board.
- K10 layout changes are constrained by the physical display and embedded
  resources, so compile success alone does not prove visual acceptance.
- Runtime orientation switching is intentionally outside the MVP path.

## Next-step triggers

- Start or replace `.tasks/ACTIVE_TASK.md` before a bounded implementation.
- Update `DECISIONS.md` when changing an architecture boundary, data flow,
  orientation strategy, build topology, or secret-handling rule.
- Update `ACCEPTANCE.md` or its examples when a reusable completion rule is
  discovered; record a milestone in `CASELOG.md` only when evidence is real.
