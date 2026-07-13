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

## 2026-07-13 — HALF-Work Git checkpoint metadata convention

**Scope.** Extend the proven control process so Git history records task level
and workflow stage without turning commit subjects into narrative reports.

**Decision.** Use a concise subject prefix such as
`[L0][Verify] fix(gesture): rearm after held tilt`, plus standard trailers for
`HALF-Work-Level`, `HALF-Work-Stage`, and `HALF-Work-Task`. A final checkpoint may
include short evidence; empty phase-only commits are not required.

**Reason.** The previous L0 case already used meaningful docs and implementation
checkpoints, but the commit subject alone did not expose whether a change was
an L0/L1/L2 task or where it sat in Observe → Plan → Implement → Verify → Review.

**HALF-Work feedback.** This preserves the existing short, implementation-first
commit style while making later review and machine-assisted history queries
possible. The convention applies to future tasks and does not rewrite history.

## 2026-07-13 — HALF-Work Phase 2 Test B: homepage 3:2 dual-zone layout

**Scope.** Execute the first Level 1 standard task after the L0 gesture case:
recompose the fixed 240x320 portrait homepage into an upper AI-usage primary
card and a lower environment-support card.

**Confirmed contract.** Scope A used existing `UiModel` fields only; Layout A
used a vertical 3:2 split; Content A made AI usage primary and environment
status secondary; Evidence B required the browser mockup plus a `DeskNest`
firmware build. Gesture, sensor state-machine, navigation, orientation, and
TokenNest protocol changes were explicit non-goals.

**Observed baseline.** The production page already had 151px and 97px cards,
but the larger card rendered `homeFocus` while AI usage was secondary and
environment values were absent from the homepage. The existing model already
provided total AI usage, provider percentages, environment readings, grade, and
advice, so no model extension was necessary.

**Change.** Replaced the homepage renderer composition with an AI card showing
total usage, a progress bar, and Codex/ChatGPT/MiniMax rows. The support card
shows environment grade, temperature, humidity, lux, and advice. Updated the
local `docs/ui-mockup/index.html` P1 examples to the same hierarchy.

**Evidence.** The 240x320 content budget is explicit: `151 + 5 + 97 = 253px`
inside the 258px content area. `C:/Users/DF/.platformio/penv/Scripts/pio.exe
test -e desknest_test` passed all 67 test cases, and
`C:/Users/DF/.platformio/penv/Scripts/pio.exe run -e DeskNest` completed
successfully. `git diff --check` passed. No board flash or physical K10 visual
acceptance is claimed. The in-app browser could not open the repository's
`file://` mockup under its URL policy, so the preview evidence is static
structure/geometry only rather than a screenshot.

**HALF-Work feedback.** The first L1 task stayed within one renderer because
the render-data boundary was already sufficient; the initial two-step contract
prevented scope drift into state-machine or data-source work. For future UI
tasks, exact geometry and mockup/renderer hierarchy parity should be explicit
acceptance items, while compile success remains separate from board-level
visual acceptance.

**Result.** Test B has a completed Evidence B checkpoint with a remaining,
explicit board-visual risk. The next task may be a board-oriented visual review
or the next planned L2 architecture test; the L1 process itself is ready for
template review.

## 2026-07-13 — HALF-Work Phase 2 Test B follow-up: merge Codex homepage row

**Scope.** Apply an L0 presentation fix to the homepage AI card after review
found that Codex and ChatGPT were shown as two separate services.

**Diagnosis.** TokenNest documents that its ChatGPT usage source reuses the
Codex CLI OAuth flow. The homepage renderer was therefore exposing one logical
Codex entry twice: once from the ChatGPT usage field and once from the separate
Codex/reset-oriented model field.

**Change.** The homepage now uses the existing ChatGPT usage value under a
fixed `Codex` label and keeps `MiniMax` as the only second provider row. The
P1 mockup examples follow the same two-row presentation. The detailed AI page,
model schema, and TokenNest protocol were left unchanged because this request
was limited to the homepage presentation.

**Evidence.** Static checks found six P1 provider rows across three preview
instances: three `Codex` rows, three `MiniMax` rows, and zero `ChatGPT` rows.
`desknest_test` passed all 67 test cases and the `DeskNest` firmware build
completed successfully. No board flash or physical visual acceptance is
claimed.

**HALF-Work feedback.** For L0 UI fixes, verify the product-facing label
against the underlying data-source semantics before adding a new visual row.
An existing model field is not automatically a separate user-facing service.

## 2026-07-13 — HALF-Work P1 follow-up: fill homepage primary card

**Scope.** Align the current P1 mockup with the production homepage and remove
the meaningless lower blank area left after the Codex/ChatGPT merge. P2–P4
repair was explicitly deferred until a later HALF-Work task.

**Change.** Kept the 220x151 AI primary card and redistributed its internal
height across the header, total usage, progress bar, Codex row, MiniMax row, and
a data-backed next-refresh row. The exact inner budget is
`18 + 32 + 8 + 22 + 22 + 18 + 5x3 + 16 = 151px`. Updated only the three P1
mockup instances and added a visible note that P2–P4 are historical previews.

**Evidence.** Static checks confirmed the production geometry and matching P1
preview structure. `desknest_test` passed all 67 test cases and the `DeskNest`
firmware build completed successfully. The local `file://` preview remained
blocked from automated refresh/screenshot by the browser URL policy, so no
automated visual screenshot is claimed. No board flash or physical visual
acceptance is claimed.

**HALF-Work feedback.** Once a task narrows to one production page, its
preview should declare which pages are current acceptance targets and which
are historical. Filling a fixed card with meaningful model data is preferable
to preserving a decorative empty region; unrelated pages should remain out of
scope until their own task contract exists.
