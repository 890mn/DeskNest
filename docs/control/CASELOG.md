# Case Log: DeskNest

## 2026-07-13 — HALF-Work Phase 1 control-plane initialization

**Scope.** Initialize the durable DeskNest control plane and its local task
contract without changing firmware source or claiming a firmware result.

**Observed baseline.** The repository uses PlatformIO with `src_dir = .`, has
the `DeskNest` and `desknest_test` environments, uses LVGL as its production
UI path, and contained one user-owned untracked glyph script.

**Work performed.** Added the project rules and 4+1+2 documents, narrowed the
documentation ignore rule so stable control documents can be tracked, kept
task-local files ignored, and synchronized the active-task resource-mode
design into HALF-Work's design source and Phase 1 roadmap.

**Evidence recorded for this case.** The completion review uses `git diff
--check`, `git check-ignore` for the tracked-control/local-task split, a
sensitive-content scan of the new files, and a final worktree check confirming
the glyph script remains untracked and untouched.

**Result.** This is a documentation and control-plane case only. No firmware
build, board flash, simulator result, or product-function result is asserted.

## 2026-07-13 — HALF-Work Phase 2 Test A: held-tilt navigation re-arm

**Scope.** Fix a Level 0 gesture regression in the DeskNest firmware without
changing UI, page ownership, or the fixed-orientation MVP boundary.

**Observed behavior.** When the gesture-confirmation lock was disabled, a left
or right tilt stayed above the motion threshold. The firmware emitted an
outbound navigation event, returned to `IDLE`, and emitted another event after
the cooldown expired, causing continuous page switching.

**Diagnosis.** `shake_fire_on_outbound` intentionally allowed a light outbound
trigger, but the event path immediately returned to `IDLE`. Cooldown delayed a
held-input repeat; it did not require the input to leave its active zone.

**Change.** Added `SHAKE_PHASE_WAIT_NEUTRAL`. Outbound mode now emits once and
waits for three stable samples inside the baseline-relative settle band before
returning to `IDLE`. The wait-neutral state is excluded from the ordinary shake
window timeout.

**Evidence.** `pio test -e desknest_test` passed all 67 test cases, including the
new held-tilt edge-trigger test. `pio run -e DeskNest` completed successfully.
No board flash or physical K10 acceptance is claimed by this case.

**HALF-Work feedback.** Level 0 can stay lightweight, but stateful inputs need
a minimum behavior contract: define trigger, held behavior, and re-arm; add a
negative held-state test and a positive re-arm test. This is a reusable rule,
not a reason to escalate every small fix into an architecture task.

**Result.** The L0 fix is behaviorally reasonable and backed by host regression
tests plus a firmware build. The remaining product risk is board-level feel
and sensor behavior under real sustained tilt.
