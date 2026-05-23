# Session Continuation — NEXT: P7 T5 slice 5

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. Ack any
   `[FROM OTTO → IDA]` entries (set `Status: acked YYYY-MM-DD`, add a
   `Resolution:` line, then act on the guidance). Last check: section
   was empty.
2. Read the full T5 plan at
   `/Users/larryseyer/.claude/plans/t5-insert-ui.md` — slice 5 section.

## ▶ NEXT — T5 slice 5 (operator-eyes-on)

The audible gate: drop an RVB slot on a routed strip and hear plate
reverb. All engine + popup pieces (slices 1–4) are landed on
origin/master; slice 5 wires them together via `MainComponent`.

**Files to touch:**

- **`app/MainComponent.cpp`** — extend `InputMixerPane::setStrips()`
  and `setBusStrips()` to add an "INS" button next to the existing
  destination picker. Mirror the `destButton` plumbing (LookAndFeel +
  lambda). Add parallel arrays `inputStripInsButtons_` /
  `busStripInsButtons_`. The destination picker (`showDestinationMenu`
  at `MainComponent.cpp:852-867`, button at ~line 556) is the anchor /
  popup pattern to mirror.
- **Lambda relay** (parallel to `onDestinationChosen`):
  `std::function<void (int stripIdx)> onInsertChainClicked`. On click:
  1. Read `nodeKey` from `inputStripChannelIds_[idx]` or
     `busStripIds_[idx]`.
  2. Pull the current `EffectChain` for that node (check whether
     `getChainForBus(nodeKey)` exists on the host — if not, add it
     in this slice; pure read, no synchronization risk).
  3. Construct an `ida::InsertChainPopup` seeded via `setInitialChain`
     with the Internal-only subset.
  4. Wire the four callbacks (detach audio → engine API call →
     re-attach):
     - `onSlotChanged`        → `host.setInternalFxAtSlot(nodeKey, slot, id)`
     - `onSlotBypassToggled`  → `host.setInternalFxBypassAtSlot(nodeKey, slot, bypassed)`
     - `onSlotsReordered`     → `host.moveInternalFxSlot(nodeKey, from, to)`
     - `onClose`              → tear down popup
     Note: the popup also mutates the persisted `EffectChain` and
     calls `setEffectChain(chain)` — slice 3's chain-set sweep then
     re-propagates bypass via `setInternalFxBypassAtSlot`. This keeps
     persistence honest without a parallel state-update path.
  5. Show anchored to the clicked INS button.
- **`app/MainComponent.h`** — declarations for the relay lambdas +
  the popup `std::unique_ptr<ida::InsertChainPopup>`.

**Engine surface (already exists, do not re-implement):**

- `ida::InsertChainPopup` — public surface at
  `ui/include/ida/InsertChainPopup.h` (`setInitialChain`,
  `setOnSlotChanged`, `setOnSlotBypassToggled`, `setOnSlotsReordered`,
  `setOnClose`).
- `ida::IEffectChainHost::setInternalFxAtSlot` /
  `setInternalFxBypassAtSlot` / `moveInternalFxSlot` —
  `core/include/ida/IEffectChainHost.h`. Message-thread, audio-detached.
- Persisted `EffectChainEntry::bypassed` is wired through to the host
  on every `setEffectChain` sweep — see `engine/src/Bus.cpp` +
  `engine/include/ida/ChannelStrip.h`.

**Headless test surface:** none new. Slice 5 is operator-eyes-on by
design — JUCE drag gestures and live audio routing aren't unit-testable
in this repo. Build clean is the agent gate; the operator drives the
GUI smoke below.

**Operator gate (the audible test):**

- Drop an RVB slot on the first input strip with signal routed
  through it. Expect ~4–6 s of dry pass-through (OTTO worker IR
  load), then audible plate reverb tail.
- Toggle bypass mid-signal: instant dry. Toggle off: audible wet
  again, no re-load (adapter stays prepared).
- Add an EQ slot above the RVB; drag the EQ down past the RVB:
  chain re-orders, EQ now post-RVB.
- Save the session; close; reopen; load: insert chain restored with
  bypass state intact.

## ▶ AFTER SLICE 5

Slice 6 of the T5 plan is "refresh continue.md" — fold into the slice
5 commit. Then resume the operator's mixer→transport roadmap
(`project_mixer_then_transport_roadmap`): T4 Sends tab UI (needs an
Output Mixer tab first), or T6 P4/P5 persistence wiring into
MainComponent save/load.

## ▶ BASELINE (snapshot, may shift)

- `ctest --test-dir build`: **639 pass / 1 not-run / 640 total**. The
  not-run is the operator-only `MainComponentPluginEditorTests_NOT_BUILT`
  sentinel.
- `master` HEAD on origin: `f77c6f8` (P7 T5 slice 4 — InsertChainPopup).
- OTTO submodule SHA: `6b37609e` on `origin/main`.

## ▶ HOUSEKEEPING

- **Whitepaper:** `docs/IDA_Whitepaper_V8.md` (underscores —
  `project_whitepaper_path`).
- **Operator actions still pending** (between sessions, agent cannot
  perform; tracked in `todo.md`): local working-directory rename
  `SiriusLooper → IDA` is already done (you're in `/Users/larryseyer/IDA`);
  notarytool keychain `ida-notary` setup still pending; `automagicart.com/ida`
  product page + `larryseyer.com` rename still pending.
- **Clean build before any GUI smoke:** `rm -rf build && cmake -B build
  -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
  --target IdaTests IDA -j` (CMake caches stale configs;
  `feedback_clean_builds`).

