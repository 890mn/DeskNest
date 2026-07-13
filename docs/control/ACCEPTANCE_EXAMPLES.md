# Acceptance Examples: DeskNest

## UI consumes a model

**Accept:** A UI change adds or rearranges LVGL widgets using fields already
provided by `UiModel`, with domain calculations remaining in the model or its
own module.

**Reject:** The renderer reads sensors, decides gesture state, or performs
business policy merely to populate labels. It may appear to work, but it
breaks the render-data boundary.

## Runtime rotation stays out of the core path

**Accept:** A fixed-orientation layout adapts its composition within K10
geometry and is validated on the firmware/board path.

**Reject:** A page adds runtime display rotation as a visual shortcut without
an architecture decision, a migration plan, and device acceptance evidence.

## Credentials remain local

**Accept:** A developer fills ignored local configuration or uses a generated
local credential file; review output and tracked documentation contain no
credential values.

**Reject:** An untracked local configuration, generated secret file, token,
or endpoint detail is added to a commit, pasted into a task contract, or used
as a public example.

## Simulator evidence is supplemental

**Accept:** A simulator helps inspect an isolated rendering issue, followed by
firmware build or board evidence where the task affects device behavior.

**Reject:** A simulator-only pass is reported as proof that K10 input, layout,
or display behavior is complete.

## Fixed 240x320 split layout

**Accept:** A two-zone page records its usable pixel budget, keeps the
production renderer and mockup in the same primary/secondary order, and
separates static geometry/build evidence from the remaining board-level visual
risk.

**Reject:** A layout is accepted because the browser mockup looks balanced,
while the firmware uses a different hierarchy or the card/gap/padding total
exceeds the real content area.

## Stateful gestures are edge-triggered

**Accept:** A left/right gesture emits one navigation event while the device is
held in that direction, then re-arms only after the measured motion returns to
the neutral band for the required stable samples.

**Reject:** A cooldown is used as the only guard, so a held tilt emits another
navigation event whenever the cooldown expires.
