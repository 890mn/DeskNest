# Architecture Decisions: DeskNest

## D-001: LVGL is the sole production UI path

- **Decision:** Use `src/ui_lvgl.cpp` as the production renderer; retain
  `src/ui.cpp` only as its retired Canvas placeholder.
- **Source:** Direct repository audit of both files and the current UI entry
  points.
- **Trade-off:** This avoids duplicated rendering behavior at the cost of not
  maintaining a Canvas fallback.
- **Re-evaluate when:** A supported, tested replacement renderer is proposed
  with a migration plan and board-level acceptance evidence.

## D-002: No runtime orientation switching in the MVP

- **Decision:** Keep the production UI in its fixed display direction; runtime
  portrait/landscape switching is not a core path.
- **Source:** Existing repository history and the current LVGL initialization
  path.
- **Trade-off:** Some layouts are adapted within the fixed geometry instead of
  providing a dynamic rotation feature.
- **Re-evaluate when:** A product requirement justifies the interaction and
  rendering risk, with K10 hardware validation defined in advance.

## D-003: PlatformIO builds from the repository root

- **Decision:** Treat the repository root as the source root because
  `platformio.ini` sets `src_dir = .`; use the `DeskNest` environment for the
  firmware build and `desknest_test` for host logic tests.
- **Source:** Direct audit of `platformio.ini`.
- **Trade-off:** Build filters must keep non-firmware directories out of the
  firmware source scan.
- **Re-evaluate when:** The project moves to a conventional source layout and
  updates its filters, entrypoints, and build acceptance together.

## D-004: Credentials never enter the repository

- **Decision:** Keep WiFi and TokenNest credentials only in ignored local
  configuration or generated local files.
- **Source:** Existing ignore rules and local configuration mechanism.
- **Trade-off:** First-time setup needs a local configuration step instead of
  an immediately runnable shared credential file.
- **Re-evaluate when:** A managed secret-delivery mechanism is introduced and
  its access, rotation, and repository exposure controls are reviewed.
