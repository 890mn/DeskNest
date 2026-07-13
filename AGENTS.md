# DeskNest Project Rules

## Project facts

- DeskNest targets the DFRobot UNIHIKER K10 on Arduino through PlatformIO.
- `platformio.ini` sets `src_dir = .`; firmware builds are rooted at the
  repository root, not only `src/`.
- The main firmware environment is `DeskNest`; host logic tests use
  `desknest_test`.
- Invoke PlatformIO through the full executable path:
  `C:\Users\DF\.platformio\penv\Scripts\pio.exe`.

## Architecture boundaries

- `src/ui_lvgl.cpp` is the sole production UI renderer.
- `src/ui_model.*` is the render-data boundary. Renderers consume that model;
  business logic, state machines, and sensor logic stay outside the renderer.
- `src/ui.cpp` is a retired Canvas placeholder. Do not revive it or create a
  second production UI path.
- The current MVP keeps a fixed orientation. Do not add runtime portrait or
  landscape switching without an explicit architecture decision and board-level
  acceptance plan.

## Product and verification constraints

- Keep WiFi and TokenNest credentials in ignored local configuration or
  generated local files. Never place them in Git-tracked source or documents.
- `pc-simulator` is a helper, not the first acceptance target. Prefer the
  firmware path and K10 board behavior when they disagree.
<!-- HALF-WORK:MANAGED:FONT-HELPER:START -->
- The former one-shot helper `scripts/add_chinese_glyphs.py` was removed on
  2026-07-13 at the user's request. Use the independent CNFontNest tool for
  glyph discovery and generation; do not recreate the legacy helper.
<!-- HALF-WORK:MANAGED:FONT-HELPER:END -->

This file records stable project constraints only. Put current task scope and
progress in `.tasks/ACTIVE_TASK.md`, not here.
