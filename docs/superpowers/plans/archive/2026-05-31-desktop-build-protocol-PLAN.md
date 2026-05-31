# PLAN.md — IDA Master Build Plan

<!--
HOW TO USE THIS FILE
- Each task must be small enough to finish well under 35% context in one
  focused session. If a task can't fit, split it BEFORE starting.
- Every task carries a Definition of Done (an observable end state) and a
  Verify command (an objective pass/fail test). These are what stop work
  from being dropped or done sloppily — "done" is a test result, not a vibe.
- Check a box ( - [ ] -> - [x] ) only after its Verify command actually
  passes.
- If the real IDA plan is still in prose/phase form, the FIRST session's
  only job is to populate this file at the granularity shown below.
- The task below marked T0.1 is a worked EXAMPLE of the format. Replace it
  (and the placeholder phases) with the real IDA tasks.
-->

## Phase 0 — Plan setup

- [ ] T0.1 (EXAMPLE — replace) Add a config loader stub for IDA
  - Files: `src/ida/Config.h`, `src/ida/Config.cpp`
  - Definition of Done: `Config::load(path)` exists, returns a populated
    struct on a valid file and a clear error on a missing file; no callers
    wired yet.
  - Verify: `cmake --build build && ctest -R ConfigLoad`
  - Notes: keep INI vs JSON choice consistent with whatever PROGRESS.md
    already records; do not introduce a new format.

## Phase 1 — <name this phase>

- [ ] T1.1 <one concrete change>
  - Files: <paths>
  - Definition of Done: <observable end state>
  - Verify: `<command that returns pass/fail>`
  - Notes: <optional — constraints, gotchas to watch>

- [ ] T1.2 <next concrete change>
  - Files: <paths>
  - Definition of Done: <observable end state>
  - Verify: `<command>`

## Phase 2 — <name this phase>

- [ ] T2.1 <...>
  - Files:
  - Definition of Done:
  - Verify:

<!--
GRANULARITY CHECK for each task before you start it:
  - One sitting? If it needs more than a focused pass, split it.
  - One Verify command? If "done" needs several unrelated checks, split it.
  - Self-contained? If it depends on an unfinished task, sequence it after.
-->
