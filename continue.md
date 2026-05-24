# Session Continuation — NEXT: bump `lsfx_tapecolor` (Phase 6/7/8), then Slice 2c (param UI) + Slice 3 (whitepaper)

> **For a fresh chat picking this up cold:** memory + project + user CLAUDE.md
> load automatically. This file is the **forward-looking handoff** — only
> what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. **At the time this handoff
   was written OTTO had NOT yet posted its TAPECOLOR Phase-8 entry**
   (operator confirmation 2026-05-24); they will post when their work
   wraps. If a `[FROM OTTO → IDA]` Phase-8 entry IS now present, follow
   its `For IDA's Claude:` guidance first — it supersedes the inspection
   notes below.
2. Re-read auto-memory `[[project_tapecolor_placement]]` — the two-mode
   design (Mode A per-tape tri-state, Mode B insert-anywhere). Slice 2's
   audio hook is now LIVE for Mode A (BeforeWrite only — AfterRead is
   data-model-only until a tape-read path exists).
3. **Operator eyes-on still pending** on two shipped slices. Both should
   be verified together with input signal on ch1 + ch2:
   * `191ef5f` (TAPECOLOR Slice 2) — deeper soak of the boot-hang fix
     under longer playback.
   * `5c34ed6` (per-channel Monitor + mute kill-switch) — see the
     5-step verification block at the bottom of `## ▶ OPERATOR
     EYES-ON` below.

## ▶ OPERATOR EYES-ON — per-channel Monitor + mute kill-switch (`5c34ed6`)

Each Input strip now has a **third button** above the INS + Dest rows.
Single click cycles **Off → Monitor → Direct → Off** (musician-facing
pro-audio terms — "Monitor" = with channel processing; "Direct" = no
processing, lowest latency). Default Off. Strip mute is a kill-switch
for monitoring regardless of mode (the operator-reported bug — muted
ch1 → master meter still moving — is the leak this fixes).

Verification protocol:

1. Launch `~/Desktop/IDA`, send signal to input pair 1, leave the
   strip's third button (default label **Off**) alone → master meter is
   **still** (no startup auto-wire anymore — that's the leak source).
2. Click the button on strip 1 → label flips to **Monitor** (processed),
   master meter moves.
3. Mute strip 1 → master meter **goes still immediately** (the fix).
4. Unmute → meter resumes. Click again → **Direct** (low-latency raw
   tap), meter behaviour same. Mute → still silent.
5. Save the project, reopen → button state + output pair survive.

Full design + rationale is in commit `5c34ed6`'s message and in
`docs/IDA_Whitepaper_V8.md` §7 (the engine work realizes the §7.1
"independent per-channel choices" contract).

## ▶ NEXT (FIRST when picking up cold) — Bump `external/lsfx_tapecolor` from `d8b06b1` → `a7ba9c3`

This is the **first action** for the next chat, before any new Slice 2c /
Slice 3 work. The submodule has progressed 3 phases since IDA was last
pinned:

| Phase | SHA       | What it adds (lsfx_tapecolor) | What IDA gains |
|-------|-----------|-------------------------------|----------------|
| 6     | `8b14034` | `NoiseStage` — tape hiss with HP200+LP6k+LP12k spectral shape + envelope-gated duck (Dolby-NR-style). New `Config` fields `noiseAmount` (default `0.0f`) + `noiseModulation` (default `0.5f`). | Hiss available when operator dials it up in Slice 2c. |
| 7     | `41a2ae4` | `TapeStopStage` — variable-rate ring-buffer playback with coupled LP sweep + fade-in. New API: `triggerTapeStop()` / `releaseTapeStop()` / `isTapeStopActive()`. New `Config` field `tapeStopTime` (default `0.5f`). Audio-thread-safe trigger (single atomic). | Future performer-facing tape-stop gesture (e.g. a transport "kill" button per tape). |
| 8     | `a7ba9c3` | UI **meter atomics** exposed: `inputPeak{Left,Right}Db()`, `outputPeak{Left,Right}Db()`, `inputLufs()`, `outputLufs()` — `memory_order_acquire` reads, message-thread / any-thread safe. | Slice 2c can wire these directly into the Edit-FX panel meters. |

**Risk profile — low:**
* Existing API surface (`prepare/reset/process/scratchConfig/commitConfig/liveConfig`) is **unchanged**; IDA's `TapeColorAdapter` + `TapeColoringSink` compile against the new pin without edits.
* `TapeColorConfig::enabled` still defaults to `false` → IDA's
  default-OFF rule for every adapter still holds verbatim.
* `noiseAmount = 0.0f` default → silent until operator dials up.
* `tapeStopTime = 0.5f` default → no-op until `triggerTapeStop()` is called from somewhere; IDA calls it nowhere yet, so silent.
* `convolution_.prepare(...)` still owns the cold-boot resample cost; the boot-hang fix in `TapeColoringSink::setSampleRate` keeps it from running with `sampleRate=0`, so the bump does not re-introduce the hang.

**Steps:**
1. **If OTTO has posted a `[FROM OTTO → IDA]` Phase-8 entry**: read it,
   follow its guidance, ack per protocol.
2. `cd external/lsfx_tapecolor && git fetch origin && git checkout a7ba9c3 && cd ../..`
3. `git add external/lsfx_tapecolor`
4. Clean build + full test suite:
   ```bash
   rm -rf build
   cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target IdaTests
   ctest --test-dir build -E "(PluginEditor|MainComponentPlug)"
   ```
   Expect green (728 pass; this session lifted the baseline) since the API surface IDA uses is unchanged.
5. Clean build + launch the .app for ~30 s. Confirm CPU drops to the
   normal idle band (≤ 15%).
6. Commit: `chore: bump lsfx_tapecolor d8b06b1 → a7ba9c3 (Phase 6 noise + Phase 7 tape-stop + Phase 8 meter atomics)`
7. Push to `origin/master`.

**Do NOT bump `external/OTTO` in the same commit** — OTTO's Claude is
actively pushing TAPECOLOR Phase-8 work right now; let their next push
land first, then bump `external/OTTO` separately.

## ▶ NEXT (THEN) — TAPECOLOR Slice 2c / 2b / 3 in operator's preferred order

After the bump lands, the queued TAPECOLOR work resumes.

### Slice 2b — AfterRead audio hook (BLOCKED)

Skipped this session because **no tape-read / phrase-audio-source path
exists yet**. The decorator forwards AfterRead bit-identical on the
write side; the playback-side color happens in a symmetric decorator
when a tape-read path lands. Implement when:
1. A `TapeSource` / phrase-audio-source seam exists feeding the Output
   Mixer's phrase channels.
2. A second decorator (`TapeColoringSource`?) wraps that seam and
   applies the per-tape `TapeColorAdapter` for tapes whose mode is
   `AfterRead`.

### Slice 2c — Edit-FX param UI surface

The Edit-FX panel for `kTapeColor` is still default-OFF with no
operator-facing knobs. Slice 2c brings the full Phase-5 parameter set
(drive, mix, wow, flutter, scrape, machine IR index, quality, etc.)
into the existing internal-FX Edit-FX flow, both for:
* Mode B (insert-anywhere) — the existing internal-FX edit pane.
* Mode A (per-tape) — a Tapes-tab control surface; ties the per-tape
  mode tri-state + the per-tape adapter's `scratchConfig` to operator
  gestures.

### Slice 3 — whitepaper §6.7 + user guide

`docs/IDA_Whitepaper_V8.md`:
* New §6.7 "Tape coloration (TAPECOLOR)".
* §6.7.1 — tape-bound (None / Before-write / After-read).
* §6.7.2 — 5th internal FX (insert-anywhere).
* Cross-links from §10 (Tape capture) and §11 (Render).

Plus a user-guide section (per `[[project_user_guide_alongside_whitepaper]]`).

## ▶ ALSO QUEUED — FX-return Sends → Edit-FX button swap

Carried from earlier sessions. When an FX-return strip is selected, the
Sends tab's label becomes "Edit FX" and content becomes a single big
button that opens the FX edit surface.

## ▶ QUEUED — explicit follow-ups (see todo.md)

* **MainOutDest::HardwareOutput full removal (engine cleanup)** — added
  this session. The operator-facing UI no longer surfaces it; engine /
  persistence / tests still reference it. 30+ test refs; touches
  MixerGraph constructor terminals, persistence migration, classifyMainOut,
  applyChannel/BusMainOut, renderInputGraph hwNode branch. Own slice.
* **Hide EQ + CMP from insert-slot picker** (operator design lock
  2026-05-24): every channel has built-in EQ + CMP on the strip's tabs,
  so they must NOT appear in the insert picker.
* **TAPECOLOR IRs — offline pre-bake** instead of runtime resample
  (cold-boot / per-rate optimization; defer until measurable).
* **Anticipatory direct routing (whitepaper §7.3)** — system infers
  monitor from arm-state, playback overlap, utility-signal hints. The
  explicit operator opt-in landed this session; auto-inference is a
  later layer on top.
* **Per-channel monitor output-pair picker** — `outputPair` defaults to
  0 (the first stereo pair). The Monitor button cycles modes; a
  follow-on picker lets the operator route to any pair (parity with
  master destination per `[[project_master_routable_to_any_pair]]`).
* **EC7 (carried)** — persistence of operator-tuned EQ + CMP values.
* **Per-band bypass for EQ** — engine flag.
* **Q drag on curve** — EqCurveView gesture polish.
* **Live GR readout** — wire `CmpMeterView::setGainReductionDb` from
  `CmpAdapter`.
* **DLY + RVB tabs** — analogous slices once internal-FX adapters land.
* **Plugin scanner unblock** (`project_plugin_scanner_broken`).

## ▶ BASELINE (start of next session)

* **HEAD on origin/master:** `5c34ed6` (per-channel Monitor +
  direct-layer mute kill-switch).
* **ctest baseline:** 728 pass / 0 fail with
  `ctest -E "(PluginEditor|MainComponentPlug)"`. The two
  `MainComponentPluginEditorTests` cases (`openPluginEditor` /
  `closePluginEditor`) hang from a CLI ctest run (they spawn a real
  plugin-editor window) — run them separately via `bash bash/test-s7.sh`.
* **OTTO submodule SHA:** `d43c540` (unchanged this session; OTTO is
  actively pushing Phase-8 TAPECOLOR work — bump separately after their
  next push lands).
* **lsfx_tapecolor submodule SHA:** `d8b06b1` (still 3 phases BEHIND
  `origin/main` = `a7ba9c3` — bump is the **first action** of the next
  chat).
* **App on disk:** `build/app/IDA_artefacts/Release/IDA.app`,
  `~/Desktop/IDA` alias points at it. Clean build verified; launches
  cleanly (no boot-hang regression).

## ▶ HOUSEKEEPING

* **Whitepaper:** `docs/IDA_Whitepaper_V8.md` (Slice 3 will add §6.7).
* **Operator actions still pending** (between sessions): notarytool
  keychain `ida-notary` setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename.
* **Cross-project note**: OTTO's Claude is actively pushing TAPECOLOR
  Phase 8 (UI panel + per-bus integration + meters). The `lsfx_tapecolor`
  submodule already published Phases 6/7/8 — see the bump section. OTTO
  has not yet posted its Phase-8 `CROSS_PROJECT_INBOX.md` entry to IDA;
  read the inbox on session start in case it has landed by then.
