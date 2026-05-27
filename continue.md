# Session Continuation — Mid-brainstorm checkpoint. Whitepaper V10 + slice 4b shipped this session; OTTO integration architecture locked at decision level; formal design spec + writing-plans + implementation slices still ahead

## ▶ 0. Read these first (60 seconds)

This session became a deep architecture brainstorm after slice 4d turned out to be much larger than the prior session's "1 commit small" estimate. The brainstorm is **paused mid-flight at checkpoint**, NOT abandoned. The architecturally-significant decisions are locked; the formal spec doc + writing-plans handoff + implementation are next.

1. **Whitepaper is now V10.** Read `docs/IDA_Whitepaper_V10.md` (NOT V9 — that file is gone, renamed). Three additions vs V9, all about OTTO's place in the architecture without violating the conceptual-time / LMC commitments:
   - §4.2 paragraph: OTTO is NOT a clock-discipline source for the LMC; it supplies tempo-map data (a musical-time fact) not clock discipline.
   - §5.4 paragraph: session-level tempo map is sourced from OTTO when OTTO is playing; absent when OTTO is not (free-running looper mode); positions derived from LMC sample count + Rational BPM, never from OTTO's float-precision `positionInBeats`.
   - **NEW §5.7** — OTTO as bundled rhythm engine and tempo-map source. The architectural home for OTTO. Read this whole section — it covers the dual role, the boundary-conversion rule, the not-a-plugin commitment, the doesn't-host-3rd-party-plugins commitment, the unified-preset-manager commitment, and the top-level-tab commitment.

2. **Commits this session, in order, all on `master`:**
   - `9075c85` — feat: M-OTTO-4 slice 4b — "Add OTTO source" picker + visible OTTO band + select-highlight + remove gesture.
   - `b1dbd3b` — docs: continue.md mid-session refresh (after slice 4b).
   - `b64459c` — fix: ottoFriendlyName operator-canonical names (Kick / Snare / SideStick / ... / PlayerOut1..4).
   - `a82791a` — docs: OTTO Stereo Mix output plan + todo.md entry (queued behind 4c + 4d).
   - `65ae39d` — docs: IDA Whitepaper V10 (the three OTTO additions).
   - HEAD = `65ae39d` (verify with `git log -1 --oneline`).

3. **OTTO upstream activity this session:** none. OTTO submodule pin stays at `4cdbad3e`. The 2026-05-27 IDA→OTTO EventBus brief in `external/OTTO/CROSS_PROJECT_INBOX.md` remains `needs-ack`.

4. **Baseline.** ctest 790/791 preserved (1 not-run is the separately-built MainComponentPluginEditorTests as ever). lsfx_tapecolor pin `a812670`; sfizz pin `f5c6e29f`; OTTO pin `4cdbad3e`. All unchanged within this session.

5. **The brainstorm is paused at the design-synthesis step.** All clarifying questions answered, all architectural forks decided. The remaining work is: write the formal spec, self-review, operator review, then writing-plans handoff to produce a slice plan, then implement.

---

## ▶ 1. Locked-in brainstorm decisions (the OTTO integration architecture)

These are the operator-confirmed decisions from this session's brainstorm dialog. Treat as locked unless explicitly revisited.

### Architecture / engineering

- **OttoHost embeds OTTO's `OTTOProcessor` (the full `juce::AudioProcessor`), not just `PlayerManager`+`TransportTracker`.** This was the realization that resolved "slice 4d is much bigger than expected": instead of porting OTTO's per-block driving logic (Conductor + Pattern + MIDI dispatch + everything `OTTOProcessor::processBlock` does), embed `OTTOProcessor` directly. It IS an AudioProcessor; call `prepareToPlay(sampleRate, blockSize)` once and `processBlock(buffer, midiBuffer)` per audio block. All of OTTO's pattern playback, conductor, and MIDI machinery comes with it for free.
- **Audio thread drives `OTTOProcessor::processBlock`** instead of just `processGlobalMixer`. The 32 per-output pointer accessors (slice 4b's `getOttoOutputLeft/Right(idx)`) read through the hosted `OTTOProcessor`'s GlobalMixer. Same external API shape on IDA's side — the change is internal to OttoHost.
- **OTTO's `PluginEditor` renders inside an IDA OTTO tab** (next bullet). Created via `OTTOProcessor::createEditor()`.

### UX / product surface

- **OTTO is a top-level tab** in IDA, alongside Tapes / Input Mixer / Output Mixer / Settings. Tab labelled `OTTO` (brand visible — operator chose, treating it like Logic Pro inside macOS).
- **OTTO always present, no disable UX.** The user "uses OTTO" by hitting OTTO's Play; "doesn't use OTTO" by leaving Play unpressed. No setting, no preferences toggle, no per-session enable, no per-project enable. Without OTTO playing, the session-level tempo map is absent and IDA is the free-running looper mode (§5.4 / §5.7 of V10).
- **OTTO tab embeds OTTO's existing UI as-is.** Zero UX duplication. All OTTO features available immediately. OTTO updates land in IDA on the next submodule SHA bump. Visual coherence is automatic because IDA and OTTO already share the same L&F substrate (`ui/lookandfeel/` consumed from the OTTO submodule).
- **Persistent IDA-wide transport bar at top or bottom of the main window.** Visible from every tab. Drives OTTO's transport (no separate "IDA transport" — OTTO IS the transport from the user's perspective). Mirrors state via OTTO's `TransportTracker` EventBus → IDA's existing `IOttoTransportListener` subscription (slices 4a/4b already wired).
- **Unified preset browser with branded categories.** "OTTO Patterns" / "OTTO Kits" / "OTTO Songs" appear as first-class categories alongside IDA's own preset categories. Loading an OTTO preset routes to `OTTOProcessor::setStateInformation`. Saving an IDA project captures OTTO's `getStateInformation` blob inside IDA's session JSON envelope.
- **Existing slice 4b "Add OTTO source ▶" picker label** stays as-is (operator confirmed: brand visible). Per-output friendly names already updated to operator-canonical (Kick / Snare / SideStick / Hats / Tom1..4 / Crash1..2 / Ride1 / Ride1Bell / Ride2 / Ride2Bell / Splash / China / Tambourine / Cowbell / Bongos / Congas / Shaker / Cabasa / Claps / Snaps / RevDelay1..4 / PlayerOut1..4) in commit `b64459c`.
- **Per-player stereo outputs + all-4-players stereo mix** both required. Per-player already shipped (slice 4b's PlayerOut1..4). All-4-players stereo mix specced in `docs/superpowers/specs/2026-05-27-otto-stereo-mix-output.md`; queued behind 4c+4d.

### Architectural commitments / boundaries

- **OTTO is PART of IDA, not a plugin.** Don't frame OTTO as a plugin or a hosted product in any future code, docs, or comments. OTTO is integrated. The "OttoHost" class name is OK (it owns OTTO's runtime) but the framing matters.
- **OTTO does NOT host 3rd-party plugins inside IDA.** Plugin hosting is IDA's responsibility. If a user wants 3rd-party plugins on OTTO's audio, they route OTTO's outputs into IDA's Output Mixer strips and chain plugins on those strips. OTTO's internal FX (its own EQ / CMP / Rvb / Dly / TAPECOLOR) operate INSIDE OTTO before audio crosses the output-mixer boundary into IDA's strips.
- **OTTO's UI surfaces should HIDE any "load 3rd-party plugin" / "OutputRouter::Mode" controls** when running inside IDA. Inside IDA OTTO is always effectively in "multi-out" mode (IDA reads all 32 outputs). These OTTO-UI tweaks are needed as part of the slice that embeds OTTO's PluginEditor. (Not yet scoped in detail — flag at implementation time.)
- **All OTTO features must work in IDA.** OTTO presets, patterns, kits, songs, energy levels, fills, everything. Reason: the operator's commitment is that OTTO is part of IDA, not a tag-along.
- **IDA + OTTO must appear to be a single app.** Shared L&F is half the answer; the OTTO tab placement + persistent transport bar are the rest.
- **Without OTTO playing, IDA is a free-running looper.** Loops quantize only to other loop boundaries; no tempo grid; no bar structure. This mode is FIRST-CLASS in V10 §5.4 and §5.7, not a degraded mode. The operator's quote: "if they don't use OTTO, essentially, IDA becomes a looper with no timing reference... i.e. it runs 'wild' with loops not quantized to anything other than other loop boundaries."

### The time model — boundary-conversion rule (NEW; documented in V10 §5.7)

This is the load-bearing rule that keeps OTTO integration whitepaper-compliant.

- **OTTO publishes float doubles** (`bpm`, `positionInBeats`, `positionInSeconds`) via its `TransportTracker` → `EventBus` → IDA's existing listener subscription.
- **At the OttoHost listener boundary** (the message-thread receiver):
  - `bpm` (double) → **converted to `Rational` at receipt.** Stored as Rational forever after inside IDA. `120.0` → `Rational{120, 1}`; `120.5` → `Rational{1205, 10}`. Exact.
  - `timeSignature` (int numerator / int denominator) → already Rational-compatible; pass through.
  - `positionInBeats` (double) → **DISCARDED as authoritative.** Used only as a sanity hint for cross-checks, never as a source of truth.
- **IDA's view of "where OTTO is in musical time"** is derived from:
  - LMC sample count since OTTO started playing (integer, exact, advanced by the audio thread per block).
  - The Rational BPM at each tempo-change segment.
  - Standard tempo-map Rational math (V9 §5.4 already covered this conceptually; V10 §5.4 + §5.7 make the OTTO-specific case explicit).
- **The LMC discipline hierarchy (V10 §4.2 tier list)** — GPS / PTP / NTP / Ableton Link / local CPU monotonic — is **unchanged**. OTTO is NOT in the tier list. OTTO supplies tempo-map data (musical-time facts), not clock discipline.

---

## ▶ 2. The sliced implementation plan (sketched — needs `writing-plans` to formalize)

Not yet a formal plan. This is the operator-and-me sketch from the brainstorm; `writing-plans` will produce the rigorous slice-by-slice version with verification criteria after the design spec is approved.

| Slice | Goal | Size | M-OTTO milestone touched |
|---|---|---|---|
| **S1** | Replace OttoHost's `PlayerManager`-only embed with `OTTOProcessor` embed. Update slice 4b's `renderBlock` + per-output accessors to read through the hosted processor. Tests; ctest baseline preserved. **Operator-invisible.** | medium | M-OTTO-4 |
| **S2** | Add `MainComponent::OttoPane` (new top-level tab) that hosts `OTTOProcessor->createEditor()`. Operator can see OTTO's UX inside IDA, pick patterns, press OTTO's Play, audio flows through master. **M-OTTO-4 audibility achieved + M-OTTO-6 minimum.** | medium | M-OTTO-4 + M-OTTO-6 |
| **S3** | Persistent IDA-wide transport bar (Play / Stop / BPM / position). Mirrors OTTO's transport via existing TransportTracker → listener wiring. Boundary-conversion rule applied at the listener. | small-medium | M-OTTO-6 polish |
| **S4** | Preset manager unification. OTTO presets visible in IDA's preset manager as branded categories ("OTTO Patterns" etc.) via OTTO's `getStateInformation` / `setStateInformation` API. Loading an IDA project restores OTTO's state. **M-OTTO-5 complete.** | medium | M-OTTO-5 |
| **S5** | OTTO output strip detail-panel binding (EQ / CMP / Pan / Width). Mirror of phrase-strip pattern in OutputMixerPane. Adds `selectedOtto_` to the mutual-exclusion logic. | medium | M-OTTO-4 polish (original slice 4c scope, part 1) |
| **S6** | OTTO output strip routing + persistence (DEST picker per OTTO strip; survives save/load). | medium | M-OTTO-4 polish (original slice 4c scope, part 2) |
| **S7** | OTTO Stereo Mix output. Per `docs/superpowers/specs/2026-05-27-otto-stereo-mix-output.md`. | small-medium | M-OTTO-4 extension |

Recommended order: **S1 → S2 → S3 → S4** for the audible-end-to-end critical path; S5–S7 fill out the polish + variation surface after that.

---

## ▶ 3. Where the brainstorm sits — what's left

Per the `superpowers:brainstorming` skill flow:

| Step | Status |
|---|---|
| BS-1 — Explore project context | ✅ Done |
| BS-1b — Deep whitepaper read | ✅ Done (V10 added inline as the result) |
| BS-2 — Ask clarifying questions | ✅ Done (5 questions answered, all forks resolved) |
| BS-3 — Propose 2-3 approaches with trade-offs | Partially done in dialog (the OTTOProcessor-embed approach won by elimination); could be reformalized in the spec |
| BS-4 — Present design in sections, get approval | Partially done in dialog (Architecture section presented; remaining sections: Components, Data flow, Error handling, Testing) |
| **BS-5 — Write design doc + self-review** | **NEXT** |
| **BS-5b — Whitepaper sync pass** | ✅ DONE EARLY (whitepaper V10 already shipped) |
| **BS-6 — User reviews spec** | After BS-5 |
| **BS-7 — Invoke writing-plans skill** | Terminal step |

**Target path for the next session:** Resume at BS-5 (write the design doc). Save it to `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md` (or similar). Cover Architecture / Components / Data flow / Error handling / Testing — the §1 decision list above is the input, the V10 whitepaper §5.7 is the doctrinal anchor. Self-review for placeholders / consistency / scope / ambiguity. Then ask the operator to review the file. Then invoke `superpowers:writing-plans` to produce the formal slice plan from the spec.

---

## ▶ 4. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| HEAD | `65ae39d` (`git log -1 --oneline`) |
| `git status --short` | clean (sfizz submodule shows as `m` — expected) |
| Whitepaper path | `docs/IDA_Whitepaper_V10.md` (V9 file gone; CLAUDE.md / continue.md / todo.md path refs updated; historical plan docs at `docs/superpowers/plans/2026-05-24-whitepaper-v9-conformance.md` + `2026-05-25-file-input.md` retain V9 refs as frozen records) |
| lsfx_tapecolor pin | `a812670` — unchanged |
| OTTO submodule pin | `4cdbad3e` — unchanged within this session |
| sfizz submodule pin | `f5c6e29f` — unchanged |
| ctest baseline | **790/791** (1 not-run is the separately-built MainComponentPluginEditorTests, same as before) |
| `[otto-host-render]` | 6 cases / 157 assertions green |
| `[audio-callback][otto-render]` | 2 cases all green |
| `[otto-host-transport]` | 6 cases / 30 assertions green |
| `[tapecolor-adapter]` | 5/5 green |
| IDA app builds + links | yes (clean Release build done after slice 4b) |
| Operator eyes-on of slice 4b | still pending (right-click / long-press OutputMixerPane blank area → "Add OTTO source ▶" lists 32 canonical names; pick → strip appears; remove gesture works). Audible verification of M-OTTO-4 deferred to slice S2 above. |

---

## ▶ 5. Resume protocol for next chat

1. **Read this file** (you're doing it).
2. **At session start: read `external/OTTO/CROSS_PROJECT_INBOX.md`** per the cross-project protocol. The 2026-05-27 IDA→OTTO EventBus brief should still be `needs-ack` unless OTTO's Claude has landed the fix between sessions. If OTTO posted a fix-and-bump entry, bump the OTTO submodule + ack per protocol.
3. **Read the whitepaper additions in V10**: §4.2 OTTO-not-a-discipline-source paragraph, §5.4 OTTO-tempo-map paragraph, and the entire NEW §5.7 (around line 406 of `docs/IDA_Whitepaper_V10.md`). These are the doctrinal anchors for the design spec.
4. **Resume at BS-5** — write the design spec at `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md`. Use §1 above as the locked-in input. Cover Architecture / Components / Data flow / Error handling / Testing. Self-review. Ask operator to review.
5. After operator approval: **invoke `superpowers:writing-plans`** to formalize the §2 slice sketch into a rigorous implementation plan.
6. Then start S1 (OttoHost embeds OTTOProcessor). That's the engine-only slice that unlocks everything else.

Reference docs:
- **Whitepaper V10:** `docs/IDA_Whitepaper_V10.md` (canonical "why" — read §3 + §4 + §5 for the timing model; §5.7 for OTTO specifically)
- **OTTO integration sequencing (background):** `docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md` (predates this session's brainstorm; some of its assumptions are now superseded — most notably M-OTTO-5 is no longer "low priority")
- **OTTO integration design (foundational decisions):** `docs/superpowers/specs/2026-05-22-otto-integration-design.md` (also predates this session; cross-reference)
- **OTTO Stereo Mix plan:** `docs/superpowers/specs/2026-05-27-otto-stereo-mix-output.md` (slice S7's content)
- **Cross-project inbox protocol:** `external/OTTO/CROSS_PROJECT_INBOX.md` + the matching `## Cross-Project Inbox Protocol` sections in both `CLAUDE.md` files

Memory (key entries):
- `project_otto_integration_locked_decisions` *(written this session)* — the §1 lock-in list, durable across sessions
- `project_otto_is_part_of_ida_not_a_plugin` *(written this session)* — directional principle
- `project_otto_does_not_host_plugins` *(written this session)* — IDA hosts plugins; OTTO doesn't
- `project_otto_as_output_mixer_source` — the 32-output flow into Output Mixer (still current)
- `project_otto_is_the_transport_source` — IDA has no engine-side transport; OTTO supplies (still current)
- `project_otto_is_a_submodule_now` — submodule consumption model (still current)
- `project_cross_project_inbox_protocol` — AI-to-AI handoff mechanics
- `feedback_ios_long_press_pairs_right_click` — paired gesture rule
- `feedback_sirius_done_right_and_complete` — no half-baked features
- `feedback_clean_builds` — clean rebuild before operator eyes-on

---

*End of session. Whitepaper V10 shipped with three OTTO additions clarifying the architecture (no rewrites of §3 / §4 / §5 conceptual-time + LMC commitments; no rewrites of §6 mixer architecture; no rewrites of §11 polymetric/polytemporal). Slice 4b operator-facing picker UI + visible OTTO band shipped earlier in this session; per-output friendly names corrected to operator-canonical. OTTO integration brainstorm paused at design-synthesis step with all architectural forks decided; next session writes the formal spec and hands off to writing-plans. ctest 790/791 baseline preserved throughout.*
