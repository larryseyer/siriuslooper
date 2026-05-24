# Session Continuation — NEXT: TAPECOLOR Slice 2c (param UI / Edit-FX) + Slice 3 (whitepaper §6.7)

> **For a fresh chat picking this up cold:** memory + project + user CLAUDE.md
> load automatically. This file is the **forward-looking handoff** — only
> what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. As of last session OTTO's
   Claude is actively working on TAPECOLOR (operator FYI 2026-05-24); when
   reading the inbox watch for any `[FROM OTTO → IDA]` entries that affect
   the `lsfx_tapecolor` submodule contract. IDA did NOT bump
   `external/lsfx_tapecolor` this session.
2. Re-read auto-memory `[[project_tapecolor_placement]]` — the two-mode
   design (Mode A per-tape tri-state, Mode B insert-anywhere). Slice 2's
   audio hook is now LIVE for Mode A (BeforeWrite only — AfterRead is
   data-model-only until a tape-read path exists).
3. Operator eyes-on still pending: any deeper soak of `191ef5f` (the .app
   launched cleanly at the end of the session — boot-hang fix verified).

## ▶ DONE THIS SESSION

One commit on `origin/master`:

### `191ef5f` — TAPECOLOR Slice 2: per-tape tri-state + BeforeWrite hook

**Data model (core/persistence):**
* `core/include/ida/TapeColorMode.h` — new enum
  `TapeColorMode { None, BeforeWrite, AfterRead }`. Wire-stable tokens
  ("None" / "BeforeWrite" / "AfterRead").
* `TapeDescriptor` gains a `tapeColor` field, default `None`.
* `SessionFormat::serializeTapePool/deserializeTapePool` round-trip the
  new field. Back-compat: a missing `tape_color` key reads as `None`.
  An unknown token throws (fail loud, not silently snap to default).

**Audio path (engine):**
* New `ida::TapeColoringSink` (`engine/include/ida/TapeColoringSink.h` +
  `engine/src/TapeColoringSink.cpp`). Decorator that wraps `ITapeSink`,
  owns one `TapeColorAdapter` per registered tape, and intercepts
  `deliverTapeBlock`:
  - mode `None` / `AfterRead` / unknown tape → bit-identical passthrough
  - mode `BeforeWrite` → adapter runs in-place on a pre-allocated
    scratch, then forwards colored bytes to the inner sink
* Pre-reserves `kMaxTapes = 64` entries so the audio thread never
  observes a reallocation (matches `InputMixer::tapeTerminals_`).
* `TapeColorAdapter` now exposes `scratchConfig()` / `commitConfig()` /
  `liveConfig()` (forwarders into `lsfx::TapeColorProcessor`), matching
  the other internal-FX adapters and used by `TapeColoringSink` to
  publish per-tape parameters under the config-swap pattern.

**MainComponent wiring (app):**
* Standalone app's audio graph now reads:
  `InputMixer → TapeColoringSink → FlacTapeSink`.
* `addTape` / `removeTape` keep the decorator's tape registry in lockstep
  with `tapePool_` / `inputMixer_` inside the same audio-callback-detached
  bracket. New tapes' mode follows `TapeDescriptor::tapeColor` (None on
  fresh adds; honors loaded-project values).
* Project-load path re-seeds the decorator from the loaded pool.
* `rebuildInputStrips` calls `tapeColoringSink_->setSampleRate(...)`
  symmetrically with the existing `flacTapeSink_->setSampleRate`.

**Boot-hang fix (the only non-trivial detour):**
* Initial wiring hung the .app at 99% CPU. Root cause: `rebuildInputStrips`
  fires `setSampleRate` with `audioCallback_->currentSampleRate()`, which
  is **0 before the callback has started**. Forwarding `sampleRate=0` to
  `juce::dsp::Convolution::prepare` divides by zero inside its
  `ResamplingAudioSource` and never returns.
* Fixed in `TapeColoringSink::setSampleRate` with two guards: drop
  bogus values (rate ≤ 0 or block ≤ 0) and short-circuit when params
  are unchanged (also dodges a juce-internal race when re-prepare
  collides with an in-flight IR worker load).

**Tests:**
* 4 new TapePool/persistence cases (default value, round-trip, missing-field
  back-compat, unknown-token reject).
* 8 new `TapeColoringSink` cases (passthrough × modes, BeforeWrite
  alters signal only when processor enabled, AfterRead never colors on
  write path, removeTape lifecycle, multi-tape independence,
  unknown-tape no-op).
* `ctest -E "(PluginEditor|MainComponentPlug)"`: **708 pass / 0 fail** —
  the exclusion drops the operator-only plugin-editor lifecycle binary
  (still run separately via `bash bash/test-s7.sh`).

## ▶ NEXT — TAPECOLOR Slice 2b: AfterRead audio hook (BLOCKED)

Skipped this session because **no tape-read / phrase-audio-source path
exists yet**. The decorator forwards AfterRead bit-identical on the
write side; the playback-side color happens in a symmetric decorator
when a tape-read path lands. Implement when:
1. A `TapeSource` / phrase-audio-source seam exists feeding the Output
   Mixer's phrase channels.
2. A second decorator (`TapeColoringSource`?) wraps that seam and
   applies the per-tape `TapeColorAdapter` for tapes whose mode is
   `AfterRead`.

The per-tape `TapeColorAdapter` instances on `TapeColoringSink` could
be shared with `TapeColoringSource` (single DSP unit per tape), or
each side could own its own (cleaner lifecycle, slightly more state).
Decide when the playback path lands.

## ▶ NEXT — TAPECOLOR Slice 2c: param-UI surface (operator visibility)

The Edit-FX panel for `kTapeColor` is still default-OFF with no
operator-facing knobs. Slice 2c brings the full Phase-5 parameter set
(drive, mix, wow, flutter, scrape, machine IR index, quality, etc.)
into the existing internal-FX Edit-FX flow, both for:
* Mode B (insert-anywhere) — the existing internal-FX edit pane.
* Mode A (per-tape) — a Tapes-tab control surface; ties the per-tape
  mode tri-state + the per-tape adapter's `scratchConfig` to operator
  gestures. Note: the Tapes-tab UI itself is still gated behind the
  broader tape-subsystem UI slice (`[[project_tape_pool_and_phase6_gating]]`).

## ▶ NEXT — TAPECOLOR Slice 3: whitepaper §6.7 + user guide

`docs/IDA_Whitepaper_V8.md`:
* New §6.7 "Tape coloration (TAPECOLOR)".
* §6.7.1 — tape-bound (None / Before-write / After-read).
* §6.7.2 — 5th internal FX (insert-anywhere).
* Cross-links from §10 (Tape capture) and §11 (Render).

Plus a user-guide section (per `[[project_user_guide_alongside_whitepaper]]`).

## ▶ ALSO QUEUED — FX-return Sends → Edit-FX button swap

Carried from earlier sessions. When an FX-return strip is selected, the
Sends tab's label becomes "Edit FX" and content becomes a single big
button that opens the FX edit surface. Currently FX-return selection
still hides Sends entirely on the output side (InputMixerPane already
shows real sends since `b1f2d08`).

Implementation:
1. `ChannelDetail`: per-tab label override.
2. `ChannelDetailSendsTab::setEditFxMode(bool)`: hide send cards +
   pre-fader toggle, show a single Edit-FX button.
3. New listener method `sendsTabEditFxRequested()`.
4. Wire from both panes; MainComponent routes to the FX edit surface.

## ▶ QUEUED — explicit follow-ups (see todo.md)

* **Output Mixer master meter while inputs route ONLY to tape**
  (operator-flagged 2026-05-24): if Direct Layer monitoring is OFF for
  every input pair and the master still moves, that's a leak in the
  Output Mixer routing — investigate.
* **Hide EQ + CMP from insert-slot picker** (operator design lock
  2026-05-24): every channel has built-in EQ + CMP on the strip's tabs,
  so they must NOT appear in the insert picker. UI-side filter + test
  to pin the contract. (`todo.md`.)
* **TAPECOLOR IRs — offline pre-bake** instead of runtime resample
  (cold-boot / per-rate optimization; defer until measurable). Touches
  the `lsfx_tapecolor` submodule, coordinate with OTTO. (`todo.md`.)
* **EC7 (carried)** — persistence of operator-tuned EQ + CMP values.
* **Per-band bypass for EQ** — engine flag.
* **Q drag on curve** — EqCurveView gesture polish.
* **Live GR readout** — wire `CmpMeterView::setGainReductionDb` from
  `CmpAdapter`.
* **DLY + RVB tabs** — analogous slices once internal-FX adapters land.
* **Plugin scanner unblock** (`project_plugin_scanner_broken`).

## ▶ BASELINE (start of next session)

* **HEAD on origin/master:** `191ef5f` (TAPECOLOR Slice 2: per-tape
  tri-state + BeforeWrite hook).
* **ctest baseline:** 708 pass / 0 fail with
  `ctest -E "(PluginEditor|MainComponentPlug)"`. The two
  `MainComponentPluginEditorTests` cases (`openPluginEditor` /
  `closePluginEditor`) hang from a CLI ctest run (they spawn a real
  plugin-editor window and need an active GUI session) — run them
  separately via `bash bash/test-s7.sh`.
* **OTTO submodule SHA:** unchanged this session.
* **lsfx_tapecolor submodule SHA:** unchanged this session.
* **App on disk:** `build/app/IDA_artefacts/Release/IDA.app`,
  `~/Desktop/IDA` alias points at it. Clean build verified; launches
  to ~5-15% CPU after the boot-hang fix.

## ▶ HOUSEKEEPING

* **Whitepaper:** `docs/IDA_Whitepaper_V8.md` (Slice 3 will add §6.7).
* **Operator actions still pending** (between sessions): notarytool
  keychain `ida-notary` setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename.
* **Cross-project note**: OTTO's Claude is actively working on TAPECOLOR
  (operator FYI 2026-05-24). Watch the inbox for any submodule API
  changes that need an `lsfx_tapecolor` SHA bump on the IDA side.
