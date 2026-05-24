# Session Continuation — NEXT: brainstorm + spec the Monitor / Control-Room path (whitepaper gap)

> **For a fresh chat picking this up cold:** memory + project + user CLAUDE.md
> load automatically. This file is the **forward-looking handoff** — only
> what's next matters.

## ▶ FIRST — Read these two memories before asking anything

* `[[project_monitor_path_gap]]` (saved 2026-05-24, this chat). Operator
  design lock: **DIR (raw direct)** → that input's own Output Mixer
  channel; **MON (processed direct)** → a **Monitor path** that the
  whitepaper currently does NOT specify (pro-DAW Control Room /
  Monitor section). Without it, DIR and MON collapse into the same
  destination and are indistinguishable — the bug surface the operator
  hit on `5c34ed6`.
* Whitepaper §7.2 (lines 695–705 of `docs/IDA_Whitepaper_V8.md`) — both
  modes can be active SIMULTANEOUSLY with INDEPENDENT destinations.
  Current code's three-state mutually-exclusive cycle button (Off /
  Monitor / Direct, both hard-wired to output pair 0) is wrong-shape
  vs spec.
* Whitepaper §5.2 (line 552) — *"Direct-layer channels. Signals routed
  via the direct layer arrive at their own channels in the output
  mixer, just like Constituent-rendered signals."* Our implementation
  bypasses the OutputMixer and writes the physical output buffer
  directly. Operator confirmed the spec-aligned model is the target.

## ▶ SECOND — Run the brainstorming skill end-to-end

The operator explicitly invoked `superpowers:brainstorming` mid-chat and
asked to "plan this professionally" in a fresh session. Follow the skill
checklist strictly:

1. **Explore project context** (already covered above).
2. **Visual companion offer** — *as its own message, no other content*.
   This will involve UI / topology / mixer diagrams; visuals will help.
3. **Ask clarifying questions** — one at a time, multiple-choice.
4. **Propose 2–3 approaches** with tradeoffs + recommendation.
5. **Present design sections** for incremental approval.
6. **Write design doc** to
   `docs/superpowers/specs/2026-05-24-monitor-routing-design.md`.
7. **Spec self-review** (placeholders / consistency / scope / ambiguity).
8. **Operator reviews the written spec** — wait for explicit OK.
9. **Invoke `superpowers:writing-plans`** next (NOT a frontend or
   engine implementation skill; that comes after the plan lands).

## ▶ THIRD — Brainstorm scope (the questions queued for the new chat)

The brainstorm has not yet asked its second question — it was paused after
the operator's "let's go into design mode" insight. The fresh chat must
work through (one at a time):

1. **Monitor path topology.** Single global Monitor bus (BusKind::Monitor
   on the Output Mixer) vs multiple operator-creatable Monitor buses
   (each ≈ a cue mix / headphone send) vs separate top-level Monitor
   Section entity outside the Output Mixer entirely. Pro-DAW analogs:
   Cubase Control Room (multiple "Studio" cue sends inside one Control
   Room), Pro Tools Monitor section (main + alt monitor), Logic
   (simpler output-bus assignment).
2. **Monitor source mix model.** Does each Input channel have a
   dedicated "Monitor send" (post-strip, like a fifth send alongside
   FX-return sends) that lands in the Monitor bus(es)? Or does the
   Monitor bus copy the master mix? Or operator picks per source?
3. **Hardware-output assignment.** Each Monitor path picks its physical
   output pair (mirrors `[[project_master_routable_to_any_pair]]`).
   On 2-channel devices the Monitor path degenerates / hides.
4. **MON button → which monitor?** With multiple Monitor paths, the
   per-Input-channel MON toggle needs a target picker — or auto-routes
   to "the default monitor." Lifecycle question: how does the operator
   build cue mixes per performer?
5. **DIR's channel lifecycle.** When DIR is enabled on Input ch N, does
   the Output Mixer auto-create a "DIR: In N" channel (mirrors how
   phrase channels auto-appear per pill)? Or pre-existing slot? Or
   operator-named? Strip-level affordances (gain, pan, send, route)
   should match other Output Mixer channels.
6. **Auto-inference (whitepaper §7.3).** The current explicit MON/DIR
   toggles are the operator-locked behavior; auto-inference from
   arm-state / playback overlap / utility-signal hints is the layer
   on top. Confirm the explicit-toggle UX before adding auto.

## ▶ FOURTH — After spec lands

* **Whitepaper edit** — add a new section (likely §7.6 or a fresh §8)
  "The Monitor Path" describing the agreed topology, plus cross-links
  from §5.2 and §6.6. Per `[[project_user_guide_alongside_whitepaper]]`
  also add the user-guide chapter once the engine slice lands.
* **Implementation plan** (via `superpowers:writing-plans`) — likely
  decomposes into 3–4 slices: engine (Monitor bus + Monitor sends +
  DirectLayer → OutputMixer channel rewrite), UI (per-Input MON+DIR
  paired toggles + Monitor-path strip in Output Mixer), persistence
  (round-trip), then test/operator-verify.
* **Master meter side-fix** — task #7 from prior chat. If the spec-
  aligned design routes direct signals THROUGH the OutputMixer (as
  channels), the master meter goes alive again by construction (the
  direct-channel's contribution flows into master if it's routed there
  / the Monitor path's metering catches the rest). If the design keeps
  any bypass, AudioCallback needs to meter the physical output pair the
  master is assigned to and push into the master bus's peak atomics.
  Resolve as part of the design.

## ▶ LANDED THIS CHAT (committed + pushed will be at `f6d894b`+)

* `f6d894b` — **fix: delete OutputMixer M5 input-proxy leak; add explicit
  per-channel audio source seam**. The "phrase channel pipes raw device
  input N → master at unity" placeholder was the actual signal path
  bypassing the Monitor button. New `setChannelAudioSource(id, L, R)`
  API is the explicit source seam; default = nullptr = silent (correct).
  9 OutputMixer tests refactored to inject test buffers via the new
  setter; new `OutputMixerPhraseChannelSilenceTests` pins the silence
  rule. **729/729 ctest green**, clean rebuild done, app launches
  cleanly. (Subject to push — confirm `git log origin/master..HEAD` is
  empty when this lands.)

## ▶ STILL UNFIXED (pinned, but out of scope until the new spec)

* **MON (Processed direct) is silent** — `AudioCallback.cpp:84` passes
  an empty span for processed-channel buffers (`std::span<const
  ProcessedChannelBufferView> {}`). Documented as deferred M5→M6 and
  never plumbed. The brainstormed design replaces this whole path
  (direct signal becomes an OutputMixer channel), so no point fixing
  in-place. Operator confirmed.
* **DIR (Raw direct) audibility** — operator did NOT confirm whether
  Direct mode produces speaker audio. New chat should ask first thing
  (alongside the brainstorm; one A/B test in the app: cycle to Direct,
  do you hear input?). If yes → confirms Raw routing is alive even
  though metering doesn't reflect it. If no → there's a deeper raw-
  routing regression to chase.
* **Master meter dead with monitoring on** — the master bus only meters
  its own mix buffer; DirectLayer writes downstream to the physical
  output. Will be resolved by the spec design (see "Master meter side-
  fix" above).
* **`continue.md` (this file) needs to mention next-chat order:** read
  the two memories, then run the brainstorming skill from the top.

## ▶ ALSO STILL QUEUED (from prior chats — unchanged)

* **TAPECOLOR `lsfx_tapecolor` SHA bump** `d8b06b1` → `a7ba9c3` (3
  phases: noise + tape-stop + meter atomics). Still pending; deferred
  again this chat because the operator-reported Monitor leak took
  precedence. Procedure unchanged — see prior commits' continue.md
  history (`git log --grep='continue.md'`) or just bump after the new
  spec is committed.
* **OTTO inbox** — `external/OTTO/CROSS_PROJECT_INBOX.md` has three
  open `[FROM OTTO → IDA]` TAPECOLOR phase-6/7/8 entries that need
  ack + SHA bump per protocol. Read at session start, act per the
  per-entry `For IDA's Claude:` guidance.
* **MainOutDest::HardwareOutput full removal**, **Hide EQ + CMP from
  insert-slot picker**, **TAPECOLOR IRs offline pre-bake**, the rest of
  the `todo.md`-tracked work — all unchanged.

## ▶ BASELINE (start of next session)

* **HEAD on origin/master:** `f6d894b` (M5 proxy fix; pushed after this
  file is committed too).
* **ctest baseline:** **729 pass** / 0 fail with
  `ctest -E "(PluginEditor|MainComponentPlug)"`. New
  `OutputMixerPhraseChannelSilenceTests` adds one test over the prior
  728-baseline; tag `[phrase-silence]`. (Two
  `MainComponentPluginEditorTests` cases run separately via `bash
  bash/test-s7.sh`.)
* **OTTO submodule SHA:** `d43c540` (unchanged this chat).
* **lsfx_tapecolor submodule SHA:** `d8b06b1` (still 3 phases BEHIND).
* **App on disk:** `build/app/IDA_artefacts/Release/IDA.app`,
  `~/Desktop/IDA` alias points at it. Clean build verified this chat;
  launches cleanly (Monitor button reachable, three-state cycle visible
  on every input strip). PID at chat-end: 42138 (still running unless
  the operator kills it before the new chat starts).

## ▶ HOUSEKEEPING

* **Whitepaper:** `docs/IDA_Whitepaper_V8.md`. New Monitor-path section
  lands after the brainstorm + spec; do not edit it ad-hoc in the new
  chat.
* **Spec file:** `docs/superpowers/specs/2026-05-24-monitor-routing-
  design.md` — to be created during the brainstorm. Path is fixed by
  the brainstorming skill convention; do not relocate.
* **Auto-memory updated this chat:** `project_monitor_path_gap.md` (new),
  index row added to `MEMORY.md`. No other memory churn.
