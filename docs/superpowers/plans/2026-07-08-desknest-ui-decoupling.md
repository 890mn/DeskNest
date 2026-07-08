# DeskNest UI Decoupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decouple DeskNest UI rendering from direct global state, sensor, and gesture reads by introducing a tested UI model boundary.

**Architecture:** Keep the current runtime loop and renderer shape intact, but insert `UiModel` as a contract object between app/state/modules and UI drawing. `ui_model.h` owns pure mapping helpers that are native-testable; `ui_model.cpp` adapts existing globals into the model during the transition.

**Tech Stack:** Arduino C++ for K10 runtime, PlatformIO native + Unity for host tests, no new third-party dependencies.

## Global Constraints

- Do not rewrite the whole UI in one step.
- Preserve the current K10 canvas renderer and page appearance.
- UI renderer must move toward consuming `UiModel` instead of `g_state`, `g_sensors`, and `g_gesture` directly.
- Keep static/lightweight data structures suitable for ESP32-S3.
- Existing user changes in `src/ui.cpp` must be preserved.
- Runtime portrait/landscape switching is out of scope.
- Landscape pages and modes are reserved for a future boot-time display-orientation adaptation.

---

### Task 1: Add tested UI model boundary

**Files:**
- Create: `src/ui_model.h`
- Create: `src/ui_model.cpp`
- Create: `test/test_ui_model/test_ui_model.cpp`

**Interfaces:**
- Consumes: `StateSnapshot`, simple sensor input values, shake phase/direction.
- Produces: `UiModel`, `dn_build_ui_model_from_inputs(const UiModelInputs&)`, `dn_build_ui_model()`.

- [ ] **Step 1: Write the failing test**

Create `test/test_ui_model/test_ui_model.cpp` with tests for overview fields, face-down flags, and shake animation mapping. Do not add runtime rotation tests.

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e desknest_test -f test_ui_model`  
Expected: FAIL because `src/ui_model.h` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create `src/ui_model.h` with pure structs and inline builder. Create `src/ui_model.cpp` with the transition adapter that reads existing globals.

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e desknest_test -f test_ui_model`  
Expected: PASS.

### Task 2: Route UI through UiModel

**Files:**
- Modify: `src/ui.cpp`

**Interfaces:**
- Consumes: `dn_build_ui_model()`.
- Produces: `dn_ui_render()` builds one model per frame and render helpers read model data instead of global state/sensors/gesture.

- [ ] **Step 1: Replace direct includes**

Replace `sensors.h`, `gesture.h`, `state_machine.h` includes in `src/ui.cpp` with `ui_model.h` where possible.

- [ ] **Step 2: Add frame model cache**

Add `static UiModel g_model;` and set it in `dn_ui_render()` before dispatch.

- [ ] **Step 3: Replace page reads**

Replace direct `g_state`, `g_sensors`, `g_gesture`, and UI-local AI mock reads with fields from `g_model`. Prioritize portrait, face-down, and config pages; landscape model fields may remain as reserved compatibility data.

- [ ] **Step 4: Verify build or relevant tests**

Run: `pio test -e desknest_test -f test_ui_model` and `pio test -e desknest_test -f test_gesture`.

### Task 3: Handoff checkpoint

**Files:**
- Modify: `docs/UI_CONTRACT.md` if implementation reveals contract naming adjustments.
- Modify: `docs/architecture-refactor-plan.md` if step ordering changes.

**Interfaces:**
- Produces: a concise status update explaining what is decoupled and what remains.

- [ ] **Step 1: Check direct UI dependencies**

Run: `rg -n "g_state|g_sensors|g_gesture|mock_ai_usage" src/ui.cpp`  
Expected: no matches for direct global reads.

- [ ] **Step 2: Check changed files**

Run: `git status --short --untracked-files=all`.

- [ ] **Step 3: Report status**

Report exact files changed and verification output.
