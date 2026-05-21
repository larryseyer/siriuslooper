# Session Continuation — 2026-05-21 (routing **Phase 3 input-side routing apparatus SHIPPED** to origin/master via writing-plans → subagents; full ctest 495/496 green; next = **Phase 4 per-node insert chains** in a fresh chat)

> **For a fresh chat picking this up cold:** read this whole file
> before doing anything. The user's `~/.claude/CLAUDE.md` and the
> project's auto-memory (`MEMORY.md` + `*.md` in the memory dir) are
> loaded automatically and contain the rules. This file is the
> *state*: what just shipped and what's queued next.

---

## RESUME HERE (2026-05-21 — routing **Phase 3 SHIPPED**; next = **Phase 4 per-node insert chains** in a fresh chat)

> ## ▶ START HERE: Phase 3 (input-side routing apparatus) is on origin/master; build **Phase 4** next
> Phase 3 executed end-to-end via `superpowers:writing-plans` →
> `superpowers:subagent-driven-development` (5 tasks, all engine TDD,
> controller-verified each task: read the diff, ran the suites, validated scope).
> On **origin/master** (`0a74cc8..893b9fd`, 5 feat commits). Full ctest **495/496**
> green (the 1 non-pass = documented `MainComponentPluginEditorTests_NOT_BUILT`,
> run separately by `bash/test-s7.sh`). **Do NOT re-do Phase 3.** Plan:
> `docs/superpowers/plans/2026-05-20-mixer-routing-graph-phase3.md`.
>
> **What Phase 3 landed (all pushed):**
> - `03a8361` `MixerGraph::sendEdges()` — public const-noexcept read view of the
>   send edges (made `SendEdge` public) so the input traversal can sum sources
>   into FX returns on the audio thread. OutputMixer unaffected.
> - `0cbec44` **`InputMixer` gained its OWN routing registry** — `MixerGraph graph_
>   { { Tape, HardwareOutput } }`, `std::vector<Bus> buses_` + parallel
>   `busNodeIds_`, `channelNodeIds_` (ChannelId→node), `addBus`/`addFxReturn`,
>   `kMaxInputChannels=32`/`kMaxInputBuses=64`. Constructor auto-creates a default
>   **RVB** + **DLY** FX return. `addChannel`/`removeChannel` register/drop graph nodes.
> - `244e3a3` **main-out + send API** — `MainOutDest { Tape, HardwareOutput, Bus }`;
>   `setChannelMainOutTo{Bus,HardwareOutput,Tape}`, `setBusMainOutTo*`,
>   `setChannelSend`/`setBusSend` (all delegate to `graph_`, which enforces
>   acyclicity + dest-kind). **Default RVB/DLY returns route to the hardware output**
>   (they MONITOR, they don't capture — a node routed to the tape terminal enqueues
>   every block, so an FX return defaulting to tape would write silent tape blocks).
>   Channels still default to tape (= capture); an operator-created bus defaults to tape.
> - `7848c26` + `893b9fd` **`renderInputGraph(deviceIn, n, directOut, m, samples)`** —
>   the RT-safe traversal (noexcept, static_assert'd). Steps 1–2: per channel,
>   gather device inputs → `ChannelStrip` → route main-out (tape→`enqueueToTape`
>   stereo-interleaved / hardware-output→accumulate into directOut / bus→accumulate
>   into mix buffer) + sends (read from `graph_.sendEdges()`) into FX returns.
>   Step 3: walk `evaluationOrder()`, process each bus/FX-return (`Bus::process`)
>   into its destination (bus / tape enqueue / direct-out). `[input-routing]` = 11
>   cases; `[input-mixer]` total now 23 cases / 132 assertions.
>
> **⚠ Scope (operator-confirmed via AskUserQuestion):** Phase 3 = **engine apparatus,
> tested. NOT wired into the production audio path.** `AudioCallback`/`MainComponent`
> still use the legacy `processBuffer` (per-device-channel tape) + `processDeviceInputs`
> (metering). `renderInputGraph` is the substrate the **UI phases (P6/P7)** wire to
> production — that migration + the bus-id-as-ChannelId tape identity + the
> stereo-vs-mono tape payload reconciliation are in `todo.md` (2026-05-21 entries).
> **OutputMixer was NOT touched** (the two mixers are TOTALLY separate consoles —
> memory `project_two_mixers_totally_separate`; they reuse the generic
> `MixerGraph`/`Bus` *types*, own instances). The input side reads send LEVELS from
> the graph directly (no dense `sendMatrix_`) — this is how it "reconciles the
> send-summing DSP" the spec asked for, and sidesteps the `nextChannelId_` churn a
> dense-by-id matrix would suffer.
>
> **NEXT = Phase 4: per-node insert chains.** Spec **locked**
> (`docs/superpowers/specs/2026-05-20-mixer-routing-graph-design.md`, Phase 4
> section ~lines 271-278) — **do NOT re-brainstorm.** Channels (BOTH mixers) gain
> an `EffectChain` + `IEffectChainHost` dispatch exactly as `Bus` already has, so
> inserts run on **every** node, **capped at 8 slots** (the cap applies to buses +
> FX returns too; `EffectChain` has no cap today). External **VST/CLAP** via the
> existing M7 out-of-process host. **Built-in Sirius FX as slot contents are the
> follow-on** — Phase 4 ships NO selectable-but-dead effects. Engine + host, TDD.
>
> **First moves for the next chat (Phase 4):**
> 1. Sanity: `git status` clean; `git log --oneline -6` shows `893b9fd` on origin/master.
> 2. Read the spec Phase 4 section + `engine/include/sirius/Bus.h`/`Bus.cpp` (how a
>    bus already owns `EffectChain` + `IEffectChainHost` + runs `host_->pumpSlot`)
>    + `engine/include/sirius/EffectChain.h` (no cap today) + `ChannelStrip.h` (the
>    per-channel node that gains an insert chain) + the M7 out-of-process host seam.
> 3. `superpowers:writing-plans` → `docs/superpowers/plans/2026-05-20-mixer-routing-graph-phase4.md`,
>    then `superpowers:subagent-driven-development`. Engine+host TDD, no operator eyes-on.
> 4. Acceptance (per spec): per-channel chain dispatch, 8-slot enforcement
>    (buses/returns too), bypass/reorder, RT-safety, behavior-equivalence for empty chains.
>
> **The 8 phases (full text + per-phase test coverage in the spec):**
> 1. ✅ Engine routing-graph core (`MixerGraph` + OutputMixer) — SHIPPED (Phase 1).
> 2. ✅ Multi-terminal `MixerGraph` — SHIPPED (Phase 2).
> 3. ✅ **Input routing apparatus** — SHIPPED (Phase 3, this session).
> 4. **Per-node insert chains** (engine+host TDD) — **NEXT**. Channels gain
>    EffectChain+dispatch; 8-slot cap everywhere; external VST/CLAP. (Built-in FX = follow-on.)
> 5. Routing-graph persistence (TDD) — SessionFormat round-trip; pre-graph loads clean.
> 6. Input Mixer UI: creation gesture + Bus/FXReturn strips + destination picker (operator-verified).
> 7. Input Mixer UI: sends tab + insert management ≤8 (operator-verified).
> 8. Output Mixer UI parity — gated on the output-mixer surface existing.
> Follow-on (own spec): internal Sirius FX (EQ/Comp/Rvb/Dly, seeded by OTTO) +
> the **union slot model**. "Make OUR FX available" lands here.
>
> **⛔ MANDATORY: every phase ends by updating THIS file (`continue.md`)** — what
> it shipped (commits), what's verified (ctest count), and the next phase + its
> first moves. A phase is NOT done until continue.md reflects it. (Operator's
> explicit instruction; also `feedback_update_continue_md_every_session`.)
>
> **Design decisions locked (spec + memories — read them):**
> - **The two mixers are TOTALLY separate consoles** — no shared state/buses/fx/
>   logic. Input = hardware inputs → mix/treat → **tapes**. Output = phrases →
>   mix/fx → **hardware outputs**. Mirror images; reuse generic *types*, own
>   instances. (memory `project_two_mixers_totally_separate`.)
> - **Per-channel destination picker** (bottom of strip): Input → **bus / tape /
>   hardware output**; Output → **bus / hardware output** (never tape). Input
>   direct-to-output = monitoring *through* the channel's FX. This **overturned**
>   the old "Input Mixer NEVER routes to outputs" rule. (memory
>   `project_mixer_routing_destinations_and_plugins`.)
> - **Inserts on EVERY node**, **up to 8 slots**; each slot a **union** of external
>   VST/CLAP **or** built-in Sirius FX. Channels have NO insert chain today;
>   `EffectChain` has NO cap — both net-new in Phase 4.
> - **Both mixers carry their own dedicated RVB + DLY returns.**
>
> **⚠ Carry-forward caveat (in todo.md):** the white paper's "always running /
> everything recorded" framing needs a caveat for a planned **global
> enable/disable** (system OFF = not recording). `CaptureSession` arm/disarm exists
> in `MainComponent` but may not be the intended global switch — confirm + caveat
> later. Not part of the routing graph.
>
> **Operational facts (canonical app path, OTTO read-only) are unchanged — see the
> Phase-1 historical block immediately below.**

---

## HISTORICAL — routing-graph Phase 1 + input-mixer session (superseded 2026-05-20 by Phase 2 above)

> ## ▶ Phase 1 (engine routing-graph core) is on origin/master
> The whole **Phase 1** plan executed end-to-end via `superpowers:subagent-driven-development`
> (8 tasks, all engine TDD, per-task spec + code-quality review, final holistic opus
> review = "safe to push"). It is on **origin/master** (`58184d6..c04d911`, 11 commits).
> Clean `rm -rf build` rebuild green; full ctest **478/479** (the 1 = documented
> `MainComponentPluginEditorTests_NOT_BUILT`, run separately by `bash/test-s7.sh`).
> Do NOT re-do Phase 1. Plan: `docs/superpowers/plans/2026-05-20-mixer-routing-graph-phase1.md`.
>
> **What Phase 1 landed (all pushed):**
> - `BusKind { Bus, FxReturn }` discriminator on `BusConfig` (`engine/.../Bus.h`).
> - New JUCE-free **`engine/include/sirius/MixerGraph.{h,cpp}`** — node registry
>   (Channel/Bus/FxReturn + one implicit Terminal), per-node single **main-out**,
>   leveled **sends** into FX returns, acyclic enforcement (DFS `reaches` /
>   `wouldMainOutCycle`), topological **`evaluationOrder()`** via Kahn. All mutators
>   are message-thread only; `evaluationOrder()` is the sole audio-thread read
>   (const noexcept, compile-asserted). 8 `[mixer-graph]` cases / 65 assertions.
> - **`OutputMixer`** owns a `MixerGraph(MixerTerminal::Output)`, mirrors channel/bus
>   registration into graph nodes, exposes `routeBusToBus(BusId,BusId)` (rejects
>   master-as-source, unknown ids, cycles), and walks `evaluationOrder()` in
>   `renderBuffer` Step 3 to process each non-master bus into its main-out
>   destination (master / subgroup bus / terminal). **Behavior-preserving** for the
>   default graph; **enables bus→bus subgroups**. `[output-mixer]` now 20 cases; the
>   subgroup test uses a synchronous halving `IEffectChainHost` on busB so 0.25-vs-0.5
>   genuinely proves audio traversed the subgroup (empirically fails without routing).
>
> **⚠ Phase 1 scope nuance carried INTO Phase 2 (do not forget):** `MixerGraph` models
> send **topology** only; send-level **summing DSP** still flows through `OutputMixer`'s
> existing `sendMatrix_`. **Phase 2 reconciles them** (sends drive FX-return summing on
> both mixers). Forward-looking notes from the final review (not defects, address in P2):
> a `busNodeId→busIdx` reverse lookup would remove the two O(n) scans in Step 3 once
> per-node send work grows; `reaches` rebuilds vectors per call (matters if the UI
> picker calls `wouldMainOutCycle` per mouse-move).
>
> **NEXT = Phase 2: input-side routing apparatus.** No plan written yet — run
> `superpowers:brainstorming` against the spec
> (`docs/superpowers/specs/2026-05-20-mixer-routing-graph-design.md`, Phase 2 section)
> + the roadmap below, then `superpowers:writing-plans` →
> `docs/superpowers/plans/2026-05-20-mixer-routing-graph-phase2.md`, then
> `superpowers:subagent-driven-development`. Phase 2 = add the shared `MixerGraph`
> substrate to **`InputMixer`** (terminal = **tape**; channel main-out → bus/tape;
> sends → FX returns; **absorbs the old "tape-output routing" slice**) AND reconcile
> the send-summing DSP. Engine TDD, no operator eyes-on.
>
> **Long-range roadmap (operator: "have a long range plan to finish the entire
> thing… spread over several chats… keep continue updated").** Each phase = its own
> plan (`docs/superpowers/plans/…-phaseN.md`) + (usually) its own chat:
> - **P1** Engine routing-graph core (shared `MixerGraph`) — ✅ **SHIPPED** (origin/master)
> - **P2** Input-side routing apparatus (terminal=tape; channel→bus/tape; sends→FX returns; absorbs tape-output routing; reconciles send-summing DSP) — engine TDD — **NEXT**
> - **P3** UI: blank-area creation gesture + Bus/FXReturn strips + main-out picker (Input Mixer) — operator-verified
> - **P4** UI: Sends detail tab (vendor OTTO `ChannelDetailSendsTab`) — operator-verified
> - **P5** Persistence: serialize the graph into `SessionFormat`; pre-graph sessions load clean — engine TDD
> - **P6** Output Mixer UI parity (reuses the shared engine model) — operator-verified, gated on the Output Mixer surface existing
>
> After P6: the **follow-on spec** (OTTO `PlayerIRConvolution` + `PlayerDelay` as
> internal RVB/DLY droppable into an FX return) — its own brainstorm→spec→plan.
> Open items to carry across phases: pre/post-fader send toggle (default post),
> FX-return sends (none in v1), input-side caps (P2). Do NOT re-brainstorm; the
> spec is self-contained and the design decisions are locked.
>
> **What the routing graph is (decided):** **buses** (fed by channel main-outs;
> insert FX) and **FX returns** (fed by sends only; host RVB/DLY/plugins) are
> distinct nodes; **main-out** (one per node → bus or terminal) vs **sends** (many,
> leveled, post-fader → FX returns); **flexible acyclic** routing that **defaults to
> the terminal**; created live via a **blank-area long-press 3-option menu**
> (Input: bus/FX-return/tape; Output: bus/FX-return/output); both mixers share one
> engine substrate (reuse `OutputMixer`'s bus + send matrix + `Bus`; input side
> net-new, terminal = tape). **This absorbs the old "tape-output routing" slice**
> (it's phase 2: channel main-out → tape). FX-return contents host the existing
> plugin mechanism for now.
>
> **Follow-on spec, right behind:** integrate **OTTO's reverb (`PlayerIRConvolution`)
> + delay (`PlayerDelay`)** as Sirius's internal RVB/DLY droppable into an FX return
> (operator: "use OTTO's rvb and dly… wiring first, then follow right behind").
> Brainstorm→spec when the wiring phases are landing.
>
> **Doc-tier rule (operator asked this session, now explicit):** white paper = the
> "why"/enduring architecture (no mechanism); `docs/design/mixer-design.md` = the
> decided model (decisions that could've gone another way); `docs/superpowers/specs/`
> = how we build it (APIs, files, phases, tests). Test: *true regardless of
> implementation* → white paper; *a decision* → design doc; *an API/file/phase* →
> spec. The routing graph is split across all three accordingly.
>
> Full deferral list: `todo.md`. Design: decision 9 in `docs/design/mixer-design.md`
> + whitepaper §6.6 (refined this session) + the spec.
>
> ## Pan+width slice — what shipped this slice (`3e951ef`, on origin/master)
> - **Engine (TDD):** `ChannelStrip<Audio>` gained `setWidth`/`width` (clamped
>   `[0,2]`, default 1.0 = unity) + mid/side stereo-width DSP in `process()`,
>   applied **after** pan, **before** peak/LUFS metering. The `width==1.0` unity
>   case skips the M/S fold so the pan-only path stays byte-identical. +9
>   `[channel-strip][width]` cases (full ctest 468/469; the 1 is the documented
>   `MainComponentPluginEditorTests_NOT_BUILT`).
> - **UI:** vendored OTTO `ChannelDetailPanWidTab.{h,cpp}` byte-faithfully into
>   `ui/lookandfeel/components/` (rotary L&F `drawRotarySlider` was already
>   vendored). `InputMixerPane` shows it in a fixed band **above** the strip row,
>   hidden until a strip is **selected** (single body-click → `stripChannelSelected`
>   → `onSelect`). Pan knob is industry-standard `[-1,+1]`, mapped to the engine's
>   normalized `[0,1]` at the `MainComponent` boundary (`(pan+1)*0.5`); width
>   passes through `[0,2]`. Selection is cleared + the panel hidden on
>   `rebuildInputStrips()` (channel identities change). Mono/stereo toggle stays
>   gesture-only — no new strip-face element.
>
> **Operator roadmap (remember across chats):** finish **Input Mixer** →
> **Output Mixer** (mixdown console, one channel per phrase) → then **discuss
> transport + other metering** (its own design conversation, not yet specified).
> Don't jump to transport before the mixers are done unless redirected. Memory:
> `project_mixer_then_transport_roadmap.md`.
>
> **⚠ OPEN THREAD — UI expectations (operator raised, not yet captured):** the
> operator wants to convey UI expectations not in the white paper yet (transport,
> metering conventions beyond the channel strip, navigation/layout, gestures &
> touch parity, theming) BEFORE they silently diverge from what's built. If the
> operator opens this, capture each expectation into `docs/design/` + memory as
> canonical (like the RME + dual-meter + gesture-only decisions already were).
> Two conventions already locked this session: **every channel meter is the dual
> OTTO peak+LUFS meter** (one exception the operator will name later — await it,
> don't guess) and **gestures over UI clutter** (memories
> `project_channel_meter_dual_otto`, `feedback_gesture_over_ui_clutter`).
>
> ## What shipped this session (all on origin/master)
> - `c1edc84` **Vendored OTTO `FaderMeter` + `CompactFaderStrip`** into
>   `ui/lookandfeel/components/`, substituting `juce::ComboBox` for OTTO's
>   `TouchComboBox` (which drags in the iOS touch-menu stack; Sirius is
>   desktop-first). Compiled into `SiriusLookAndFeel` (relaxed warnings,
>   vendored). The `Fader Knob.png` + fonts were already in `SiriusBinaryData`.
> - `25fbc2d` + `e588544` **Docs: the RME mono/stereo input-source model.**
>   Whitepaper V7 §6.1 ("Audio is stereo, always") refined + §6.2 input-layer
>   gains "input source format (mono/stereo, RME split/collapse)";
>   `docs/design/mixer-design.md` decision 8. **Consolidated on V7**: the old
>   `docs/Sirius_Looper.md` was deleted (user) and the website whitepaper
>   (`website/src/docs/whitepaper.md`) was **resynced wholesale from V7** (it
>   had lagged — missing the stereo invariant entirely) + `sourceFile` fixed
>   to V7. Website builds clean (`npm run build` in `website/`).
> - `e761c1b` **Engine source model (TDD).** `InputMixer` gained a per-channel
>   **input-source descriptor** (`setChannelInputSource(id, left, right,
>   stereo)`) + a new RT-safe **`processDeviceInputs(deviceIn, n, samples)`**
>   that gathers each strip's 1–2 source device channels into a stereo block
>   (mono → dual-mono both sides) and runs `ChannelStrip<Audio>::process`,
>   populating the post-fader peak meters. 4 new `[input-source]` cases (30
>   assertions). Device buffers never mutated (raw-monitor contract).
> - `50520dd` **Input Mixer tab.** `MainComponent::InputMixerPane` — a row of
>   `CompactFaderStrip`s (one stereo strip per stereo pair of active device
>   inputs, default stereo). Registers strips in `InputMixer`
>   (`addChannel` + `setChannelInputSource(2p, 2p+1, true)`); binds
>   fader→`setGain`, mute→`setMuted`, solo→solo-in-place effective mute
>   (`recomputeInputStripMutes`); meters read `peakLeft/peakRight` on the
>   existing 30 Hz `timerCallback` (`refreshInputMixer`). AudioCallback Step 2b
>   calls `processDeviceInputs`. Output combo hidden (routing is a later slice).
> - `cae0593` + `8c58a35` **RME mono/stereo split/collapse toggle (gesture-only).**
>   **Right-click (desktop) and long-press (touch, 500 ms, drag-cancels)** open a
>   "Split to two mono channels" / "Collapse to stereo" menu — no visible toggle
>   element (operator: strip too crowded on iPhone). `inputPairs_` holds the
>   per-pair mode; `rebuildInputStrips()` re-registers engine channels (1 stereo
>   strip or 2 mono-source strips) and rebuilds the pane, **bracketed by
>   removeAudioCallback/addAudioCallback** so the runtime registry mutation never
>   races the audio thread. The pane catches the gestures via `addMouseListener`
>   so vendored `CompactFaderStrip` stays unmodified.
> - `5efea7f` **macOS audio input fix.** Added `NSMicrophoneUsageDescription` to
>   the merged Info.plist + applied the `com.apple.security.device.audio-input`
>   entitlement in the **Ninja** parent re-sign (it was previously only applied
>   in the Xcode-generator path). Without these a hardened-runtime app gets
>   silent input buffers — which is why the meters read flat on first eyes-on.
> - `18ba878` **Mono-device registration fix.** A single-input device (e.g. the
>   MacBook built-in mono mic) was being registered as a phantom stereo pair
>   sourcing device ch 0+1; ch 1 doesn't exist → the stereo strip was skipped
>   (no meter) and split showed left-only. Now a 1-input (or odd-leftover)
>   channel registers as ONE mono strip (dual-mono → both meter sides move) and
>   isn't splittable.
> - `ece8843` + `1ff4b68` **Per-channel EBU R128 LUFS meter (OTTO parity), TDD.**
>   `ChannelStrip<Audio>` now carries a `sirius::LufsMeter`
>   (`engine/include/sirius/LufsMeter.h`, adapted faithfully from OTTO's
>   `otto::mixer::LUFSMeter`) fed post-fader; `prepare(sr, maxBlock)` off the
>   audio thread, `lufsIntegrated()` read on the UI timer. Input Mixer strips
>   now show the **dual peak+LUFS** meter. 3 `[lufs]` cases. **Convention: every
>   channel meter is the dual OTTO meter** (memory `project_channel_meter_dual_otto`)
>   — one exception the operator will name later.
> - `2bb5caf` **Universal mono/stereo toggle.** Replaced the earlier "single
>   input = non-splittable mono strip" rule: now **stereo = 1 strip, mono = 2
>   strips on EVERY input** (operator: "two strips if mono, one if stereo"). A
>   single physical input (built-in mic) → 1 dual-mono strip in stereo, or 2
>   independently-pannable strips (both carrying that one input) in mono. The
>   toggle always changes the strip count, so it's visible even on a lone mic.
>   (Superseded `18ba878`'s non-toggleable rule.) Operator-verified GOOD.
>
> ## The RME input-source model (the design decision this session locked)
> Input Mixer channels are **always stereo internally** (hard invariant). The
> operator chooses each channel's **source format** via the universal toggle:
> **stereo = ONE strip** (a true pair = its L/R; a single input = dual-mono),
> **mono = TWO strips** (a true pair = its two channels; a single input = two
> independently-pannable copies of the one dual-mono signal). Per **RME TotalMix**.
> The toggle is **gesture-only** — right-click (desktop) + long-press (touch,
> 500 ms, drag-cancels) — with **no visible toggle element** (strip too crowded,
> esp. iPhone). Memories: `project_input_source_mono_stereo_rme`,
> `feedback_gesture_over_ui_clutter`.
>
> ## Critical operational facts
> - **Canonical app:** `build/app/SiriusLooper_artefacts/Release/Sirius Looper.app`
>   (CMake/Ninja, ONLY copy on disk). **Desktop alias `Sirius Looper` → this.**
>   A **clean `rm -rf build` rebuild** was done before this handoff for eyes-on.
>   GUI is **operator-verified** — open the **Input Mixer** tab and confirm the
>   meter(s) move with live input, the fader rides level, M mutes, S solos.
> - **Canonical whitepaper = `docs/Sirius Looper Whitepaper V7.md`** (spaces, V7).
>   `docs/Sirius_Looper.md` is GONE (consolidated on V7). The website mirror is
>   now synced; it deploys from origin/master.
> - **OTTO at `/Users/larryseyer/AudioDevelopment/OTTO` is READ-ONLY** — copy
>   FROM it, never edit it.
> - **Engine work is TDD'd; GUI is operator-verified** (can't keep the GUI alive
>   from CLI — I can launch it, but interactive gestures + visual confirmation
>   are the operator's).
> - **ctest 459/460.** The 1 failure (`MainComponentPluginEditorTests` #460,
>   `CHECK(childPid > 0)` at line 91) is the documented pre-existing spawn-fixture
>   failure in a bare build tree — unrelated to the input mixer (spawn path
>   untouched).
> - **Deferred bugs unchanged:** (1) M8 S6 calibration sidecar not written on
>   launch; (2) M7 S9 in-app plugin loading (XPC bridge); (3) pill 4-corner ICONS.
>   All in todo.md / the historical sections below.
> - The OTTO-parity program (A–F) is in the historical block below; the Mixer
>   sub-project (B) is the active line — Input Mixer first, Output Mixer after the
>   phrase→audio render path is wired.
---

## HISTORICAL — M8 S6 (superseded 2026-05-20 by the OTTO-parity pivot)

## RESUME HERE (2026-05-20 — M8 S6 fully shipped + pushed; next move = M8 S7–S8 tape reachability scan)

> ## ✅ M8 S6 fully shipped end-to-end (direct TDD execution)
>
> M8 S6 (LMC calibration corruption recovery; V7 plan line 602) is on
> `origin/master`. 4 commits (docs + module + wiring + handoff). Implemented
> directly with TDD (not subagents — the feature is one pure module + thin
> wiring, smaller than S2/S4; same tested/reviewed/pushed quality). Clean
> rebuild from `rm -rf build`; ctest **449/450** — the 1 non-pass is
> `MainComponentPluginEditorTests_NOT_BUILT` (built+run separately by
> `bash/test-s7.sh`). +7 new `[calibration-recovery]` cases (32 assertions).
>
> **The V7 wording assumed infrastructure that did not exist** — there was no
> persisted calibration AND no loopback-measurement routine. So S6 shipped the
> buildable nucleus (the M8 S3 pattern: pure validator + caller-side notify,
> production data source deferred) and deferred the real DSP engine.
>
> **What landed:**
> - `engine/include/sirius/CalibrationStore.h` + `.cpp` — pure, JUCE-free
>   (engine public API is JUCE-free by design). `serializeCalibration` /
>   `parseAndValidateCalibration` over a canonical text doc
>   (`rateFactor=n/d` / `offset=n/d` / `pending=0|1` payload + a `sha256=` line
>   over the payload bytes, reusing core `sha256Hex`). `CalibrationLoadStatus
>   { Ok, Missing, Corrupt }`; non-Ok always returns a recoverable
>   `{identity, recalibrationPending=true}` document. `AudioDeviceCalibration`
>   itself is UNMODIFIED — `recalibrationPending` is store metadata, not a field
>   on the immutable value. `postCalibrationRecoveryNotification` posts one
>   `Warning`/`StateRepair` on non-Ok (kept out of the parse so the parse stays
>   sink-free, parallel to `postConstituentStateNotifications`).
> - `app/MainComponent.cpp` — device-scoped sidecar at
>   `userApplicationDataDirectory/Sirius Looper/calibration.json` (calibration
>   belongs to the machine, NOT the session — it never travels with a session
>   moved to other hardware). Startup hook (before `setCalibration` at the ctor's
>   audio-callback wiring): read+validate; `Ok` adopts the values, non-Ok leaves
>   identity, re-persists `{identity, pending=true}`, and warns. On-session-load
>   re-check in `chooseFileAndLoad` beside the M8 S1/S3 hooks (satisfies the
>   spec's literal "on session load").
> - docs: spec `2026-05-20-m8-s6-design.md`, plan `2026-05-20-m8-s6-plan.md`.
>
> **Honest scope notes (also in `todo.md` 2026-05-20 M8 S6 entry):**
> - **No real loopback DSP calibration engine** — nothing MEASURES a fresh
>   calibration. The V7 "trigger fresh loopback calibration" clause is unmet by
>   design: that routine doesn't exist (needs live audio I/O + hardware). On
>   corruption the app recovers to identity + posts Warning + persists
>   `recalibrationPending=true` as the seam the future engine reads. Own session.
> - **Single-device sidecar** — multi-device keying (per-interface calibration)
>   is a future extension; device-identity plumbing absent.
>
> **The single most important next move:** start M8 S7–S8 (V7 plan line 604 +
> the "tape reachability scan (Session 7-8)" breakout): tape rotation on
> disk-full — a reachability scan from the Constituent graph identifies tape
> segments referenced by no active Constituent, reclaims oldest-unreferenced
> first, on a non-realtime thread. New `engine/TapeReachabilityScan.{h,cpp}`;
> `persistence/TapeStore` gains a `reclaimUnreferenced(span<TapeId>)` API; new
> `tests/TapeReachabilityTests.cpp`. This is ALSO where the real `TapeStore`-backed
> `TapeResolver` for M8 S3's Broken detection lands (the `alwaysResolves` honest
> default gets replaced). Spec not written — brainstorm V7 line 604, then
> `writing-plans`, then implement.

### First moves for the next chat (M8 S7–S8)

1. **Sanity:** `git status` clean, `git log --oneline -6` shows the M8 S6 tail on
   `origin/master`.
2. **Read** `persistence/include/sirius/TapeStore.h` + `.cpp` (the content-addressed
   store + any existing reclaim/enumerate API), `core/.../Constituent.h` +
   `engine/ConstituentValidator.{h,cpp}` (the `TapeResolver` seam to satisfy with a
   real store-backed resolver), and the M8 S6 spec/plan for the review cadence.
3. **Check `todo.md`** for the M8 S6 follow-ups (loopback engine, multi-device
   keying), the M8 S3 follow-ups (the `TapeId→content` manifest that S7–S8 finally
   provides), and the still-open M8 S2 tri-state `requestStateSave/Load` deferral.
4. **Brainstorm M8 S7–S8**, then `writing-plans`, then implement (subagents for the
   larger surface, or direct TDD if it stays contained).
5. **Known pre-existing test note:** `bash/test-s7.sh`'s "openPluginEditor … spawns
   a child" case fails with `childPid -1` in a bare `rm -rf build` tree (the spawn
   fixture needs the built host in scope). S6 touched no spawn-path files —
   environmental, pre-existing, not an S6 regression (2/3 lifecycle cases pass).

---

## HISTORICAL — M8 S4 (superseded 2026-05-20 — M8 S6 fully shipped)

> ## ✅ M8 S4 fully shipped end-to-end (orchestrator+subagents execution)
>
> M8 S4 (WetCaptureWriter mechanism + tested Bus seam; V7 plan lines 565/581/603)
> is on `origin/master`. 4 implementation commits + 1 ceiling-comment, each gated
> by review — Task 4 (the production `Bus` change) got the full two-stage spec +
> code-quality review plus a final holistic opus review ("ready to push"); the
> test-only tasks (T2/T3) were verified directly (verbatim from the reviewed
> plan, real read-back assertions). Clean rebuild from `rm -rf build`; ctest
> **442/443** pass — the 1 non-pass is `MainComponentPluginEditorTests_NOT_BUILT`
> (the lifecycle exe isn't built by the `SiriusTests` target; built+run separately
> by `bash/test-s7.sh`). +8 new `[wet-capture]` cases (33 assertions).
>
> **What landed:**
> - `engine/include/sirius/IWetCaptureSink.h` — pure JUCE-free audio-thread
>   interface (parallel to `IEffectChainHost`): `tryEnqueueWet(ChannelId, Rational,
>   const float* const*, int, int) noexcept`. Sink owns the interleave; `Bus`
>   never names the wire layout.
> - `engine/include/sirius/WetCaptureWriter.h` + `.cpp` — captures wet
>   (post-effects) audio to `<channelId>.wet.tape.partial`, then `finalizeToStore`
>   into the content-addressed `TapeStore` (`<sha256>.tape`). A faithful parallel
>   of M3 `TapeWriter` (lock-free SPSC enqueue → worker drain → partial → finalize),
>   reusing `LockFreeSpscQueue`. `WetWriteMessage` POD carries interleaved float32
>   with `alignas(alignof(float))` samples (makes the `reinterpret_cast<float*>`
>   well-defined; the dry `TapeWriteMessage` only memcpys so it didn't need this).
>   `Error/DiskPressure` on I/O failure via `setNotificationBus`, same as TapeWriter.
> - `engine/Bus` — `setWetCaptureSink(IWetCaptureSink*, ChannelId)` (set-once,
>   non-owning, mirrors `setEffectChainHost`). `Bus::process` taps `processedBuffer_`
>   AFTER the effect chain (genuinely post-effects, proven by a DoublingHost test:
>   dry 0.25 → wet 0.5), default-off, single null-check on the hot path, clamped to
>   `activeChannels`/`clampedSamples`. RT contract (`const noexcept`, no
>   alloc/lock/throw/IO) preserved; `[bus]` noexcept static_assert still holds.
>   Bus.cpp:107 documents the 4096-stereo-frame effective ceiling
>   (kMaxTapeWriteMessageBytes < kMaxBusMixSamples) — safe drop, revisit at M9+.
> - docs: spec `2026-05-20-m8-s4-design.md`, plan `2026-05-20-m8-s4-plan.md`.
>
> **Honest scope notes (also in `todo.md` 2026-05-20 M8 S4 entry):**
> - The Bus seam is set-once plumbing, **default-off, NOT bound to any production
>   capture-arm flow** — nothing calls `setWetCaptureSink` outside tests, and
>   `MainComponent` owns no `WetCaptureWriter`. Exercised only by tests. Wire when
>   the "this `EffectChainEntry` is `ArchivalMode::WetCapture` → capture its bus
>   output" decision flow lands. This is why M8 S5 (more WetCapture) is effectively
>   absorbed: the remaining WetCapture work is production wiring that depends on
>   M9+ machinery, so the next *buildable* V7 session is S6.
> - Per-Constituent effect-chain routing → M9+; structure-layer `TapeId → wet-hash`
>   mapping → M11 SAF (same deferral as the dry tape's `TapeId → hash`).
> - `TapeWriteMessage` has the identical `reinterpret_cast` pattern WITHOUT
>   `alignas` — bring it in line when next touching `TapeWriter` (todo item 4).
>
> **The single most important next move:** start M8 S6 (V7 plan line 602: LMC
> calibration corruption recovery — on session load, validate the calibration-table
> checksum; on failure, trigger fresh loopback calibration via the existing
> `engine/.../AudioDeviceCalibration`; emit a NotificationBus event during
> recalibration). Spec not written — run `superpowers:brainstorming` against V7
> plan line 568 + line 602, then `writing-plans`, then orchestrator+subagents.

### First moves for the next chat (M8 S6)

1. **Sanity:** `git status` clean, `git log --oneline -8` shows the M8 S4 tail on
   `origin/master`.
2. **Read** `engine/include/sirius/AudioDeviceCalibration.h` + `.cpp` for the
   calibration table, any existing checksum, and the loopback-calibration entry
   point; and the M8 S4 spec/plan for the review-cadence pattern.
3. **Check `todo.md`** for the M8 S4 follow-ups (seam wiring, alignas-on-
   `TapeWriteMessage`), the M8 S3 follow-ups, and the still-open M8 S2 tri-state
   `requestStateSave/Load` deferral.
4. **Brainstorm M8 S6**, then `writing-plans`, then `subagent-driven-development`.
5. **Known pre-existing test note:** `bash/test-s7.sh`'s "openPluginEditor …
   spawns a child" case fails with `childPid -1` in a bare `rm -rf build` tree
   because the spawn fixture needs the built `.app`/host in scope (the host binary
   itself runs fine). S4 touched no spawn-path files — environmental, pre-existing,
   not an S4 regression.

---

## HISTORICAL — M8 S3 (superseded 2026-05-20 — M8 S4 fully shipped)

> ## ✅ M8 S3 fully shipped end-to-end (orchestrator+subagents execution)
>
> M8 S3 (Constituent `Valid | Broken | Invalid` state machine + render-as-silence,
> V7 plan line 601) is on `origin/master`. Tasks T1–T4 each gated by two-stage
> spec + code-quality review, plus a final holistic review. Tests: full ctest
> **437/437 pass** (was 427 at S2 ship; +10 new `[constituent-state]` cases). The
> 2 GUI-lifecycle tests (436/437) skip headlessly (pre-existing).
>
> **What landed:**
> - `core/include/sirius/ConstituentState.h` — `enum class { Valid, Broken, Invalid }`,
>   derived from the graph, NEVER persisted, never a field on `Constituent`
>   (immutable/copy-on-write preserved).
> - `engine/include/sirius/ConstituentValidator.h` + `.cpp` — `TapeResolver`
>   predicate seam + `alwaysResolves` honest default; `ConstituentValidation`
>   (`state()` returns Valid for unknown ids, `renderable()`); `validate(root,
>   resolver)` depth-first walk: **Broken** = leaf loop whose `tapeReference`
>   fails the resolver; **Invalid** = span not contained in the parent's local
>   frame (`conceptualIn()<0 || conceptualOut() > parent.duration()`); Invalid
>   dominates Broken. `postConstituentStateNotifications` posts
>   `Warning`/`Category::StateRepair` per non-Valid node (caller-side, validator
>   stays pure). `std::hash<ConstituentId>` lives in this header (core stays
>   hash-free).
> - `engine/RenderPipeline` — 3-arg ctor takes a `ConstituentValidation`;
>   `collect()` returns early on a non-Valid node, silencing it AND its whole
>   subtree (§17.7); the 2-arg ctor delegates with an all-Valid validation so
>   existing behavior is byte-for-byte unchanged.
> - `app/MainComponent.cpp::chooseFileAndLoad` — `validate(*loaded,
>   alwaysResolves)` + `postConstituentStateNotifications` on load, beside the
>   M8 S1 drift verifier.
> - docs: spec `2026-05-19-m8-s3-design.md`, plan `2026-05-19-m8-s3-plan.md`.
>
> **Honest scope notes (also in `todo.md`):**
> - Production Broken detection uses `alwaysResolves` (no TapeId→content manifest
>   yet) — only structural **Invalid** is live in-session; the real
>   `TapeStore`-backed resolver is M8 S7–S8.
> - `RenderPipeline` is NOT yet wired into the production audio callback
>   (`activeReadsAt` is test-only). The load-time validation currently drives
>   **notifications only**, not live audio silencing; the 3-arg ctor is the seam
>   for when the audio path lands.
> - Tempo-map contradiction (§17.7) intentionally OUT of S3 (revert semantics,
>   not silence).
>
> **Mid-session design topic captured (NOT acted on, by design):** the operator's
> render→parts→timeline→finished-song vision ("Ableton territory"; "when render
> is called, all tapes stop"). Recorded durably in
> `docs/design/render-and-arrangement-vision.md` + a "Future direction" pointer
> under whitepaper §6.11 (with the tension against §6.11's "render = playback to
> a file" stance flagged), plus auto-memory + `todo.md`. Deserves its own
> brainstorm→spec session.
>
> **The single most important next move:** start M8 S4 (V7 plan line 603:
> WetCapture writer, Sessions 4–5). Spec not written — run
> `superpowers:brainstorming` against V7 plan line 565 (WetCapture acceptance:
> tape writer adds `<channelId>.wet.tape` in SAF's `tapes/` subdir) + line 581
> (`engine/WetCaptureWriter.h/.cpp` new) + line 603, then `writing-plans`, then
> orchestrator+subagents.

### First moves for the next chat (M8 S4)

1. **Sanity:** `git status` clean, `git log --oneline -8` shows the M8 S3 tail on
   `origin/master`.
2. **Read** `docs/superpowers/specs/2026-05-19-m8-s3-design.md` and the plan.
3. **Check `todo.md`** for the M8 S3 follow-ups (audio-callback wiring of the
   3-arg RenderPipeline ctor; descendant-warning policy under an Invalid
   ancestor) and the still-open M8 S2 deferral (requestStateSave/Load tri-state).
4. **Brainstorm M8 S4** (WetCapture writer), then `writing-plans`, then
   `subagent-driven-development`.

---

## HISTORICAL — M8 S2 (superseded 2026-05-20 — M8 S3 fully shipped)

> ## ✅ M8 S2 fully shipped end-to-end (orchestrator+subagents execution)
>
> M8 S2 (CLAP as a first-class plug-in format + live `clap_plugin_state`
> hashing for VersionPinning) is on `origin/master`. 20 commits across
> the 12 plan tasks, every task gated by a two-stage spec + code-quality
> review. Clean rebuild from `rm -rf build` succeeds; ctest 427/427
> (was 400 at M8 S1 ship; +27 new cases). The 2 GUI-lifecycle tests
> (425/426) skip headlessly — they need a window server, pre-existing.
>
> **What landed (high level):**
> - `host/`: `ClapBundleLoader` (RAII dlopen+entry+factory, extracted
>   from `host_process/main.cpp` so the scanner + child share one load
>   path); `ClapScanner` (walks platform CLAP search paths); CLAP folded
>   into `PluginScanner` alongside VST3/AU + the `FabFilter` testing
>   filter REMOVED + per-format scan-progress notifications.
> - `core/`: `PluginStateRegion.h` — `PluginStateState` shm POD mirroring
>   `PluginGuiState` (seq-counter request/response, 64 KiB payload caps,
>   Save/Load/Status enums).
> - `host_process/main.cpp`: child services `clap_plugin_state` save/load
>   via the new state shm region (`serviceStateRequests`), tolerant of an
>   absent region for forward-compat.
> - `OutOfProcessPluginInstance::requestStateSave/Load` (engine-side IPC
>   round-trip, bounded timeout, child-reported-size clamp at the trust
>   boundary).
> - `OutOfProcessEffectChainHost::descriptorForSlot` + `stateBlobForSlot`
>   (snapshot-then-unlock so the IPC wait never holds `instancesMutex_`;
>   honors the transient bypass flag to close the supervisor race).
> - `engine/SessionSnapshot`: dropped the M8 S1 `!has_value()` re-pin
>   guard (always-refresh on save), consults the live host for real
>   state bytes via a `SlotLookup` callback, posts Info-level
>   `PluginVersionRepinned` (now carries state-hash prefixes) on save-time
>   record change, drift message reordered to `"version drift eh=.. fh=..
>   ev=.. fv=.. id=.."` (hashes first to survive 128-byte truncation).
> - `app/MainComponent.cpp`: real `slotLookup()` (one-editor-per-bus →
>   `openEditorBusIds_[entryIndex]`, slot 0) wired into save+load; scanner
>   notification sink bound to the bus.
> - `tests/fixtures/StatefulSynthFixture.cpp`: a SECOND synthetic CLAP
>   shaped like a real third-party plug-in (2 params, 1-pole filter state,
>   16-byte `SLST` state layout) — gives the suite a third-party-shaped
>   target with no external download.
> - `tests/ThirdPartyClapIntegrationTests.cpp`: opt-in via CMake var
>   `SIRIUS_THIRDPARTY_CLAP_PATH` (point it at a FabFilter/Surge `.clap`);
>   silently absent when unset.
>
> **The single most important next move:** start M8 S3 (V7 plan line 598:
> "Constituent state machine + broken/invalid render-as-silence; tests").
> Spec not written yet — run `superpowers:brainstorming` against V7 plan
> lines 598-603 first, then `superpowers:writing-plans`, then
> orchestrator+subagents.

### First moves for the next chat (concrete, ordered)

1. **Sanity:** `git status` clean, `git log --oneline -3` shows the M8 S2
   tail on top, on `origin/master`.
2. **Read the M8 S2 spec + plan:**
   `docs/superpowers/specs/2026-05-19-m8-s2-design.md` and
   `docs/superpowers/plans/2026-05-19-m8-s2-plan.md`.
3. **Check `todo.md` for the two M8 S2 deferrals** the reviews surfaced
   (both real, both non-blocking, both recorded there):
   - `OutOfProcessPluginInstance::requestStateSave/Load` collapse
     no-extension / timeout / child-error into a single `false`, which
     prevents the third-party integration test from distinguishing a
     genuine timeout from a legitimately-stateless plug-in. Needs a
     tri-state result or `lastStateError()` accessor.
   - (plus any other open items already in the file).
4. **Known M8 S2 seam (not a bug, flagged by final review):**
   `SessionSnapshot::computeLiveRecord` pins the descriptor from
   `descriptorForSlot`, which returns the CONFIGURE-TIME cached
   `SlotState::descriptor`, not a descriptor read back from the live
   plug-in — so only the STATE HASH is a live drift signal in-session;
   descriptor-version drift fires only across machines/reopens (the
   loaded value differs from the scanner-resolved one). And
   `MainComponent::slotLookup()` is positional (entry index → bus index)
   under the one-editor-per-bus model; nested chains (M11+) must resolve
   through the engine save-orchestrator instead, or entries silently drop
   to the empty-blob fallback.
5. **Brainstorm M8 S3**, then `writing-plans`, then
   `subagent-driven-development` (V7's `orchestrator+subagents` for M8).

### M8 S2 commit log (20 commits, all pushed)

| SHA | Type | Subject |
|---|---|---|
| `ff2246f` | feat | extract ClapBundleLoader shared between scanner + child |
| `fa77b50` | feat | ClapScanner walks platform CLAP search paths |
| `5df0e36` | fix | ClapScanner records non-macOS .clap dirs in failedFiles |
| `a9116a3` | feat | PluginScanner integrates CLAP + drops FabFilter filter |
| `235e659` | refactor | name PluginScanner progress-buffer + harden format-name lifetime |
| `4e107d1` | feat | PluginStateState shm region (mirrors PluginGuiState) |
| `2dd8683` | docs | clarify PluginStateState status semantics + memory-ordering |
| `62ffc39` | feat | synthetic CLAP gains clap_plugin_state fixture (4-byte) |
| `673d7d3` | feat | child services clap_plugin_state save/load via state shm |
| `0bcae42` | feat | OutOfProcessPluginInstance::requestStateSave/Load |
| `711a3a3` | fix | clamp child-reported state size + extract state-region helper |
| `3c0ffc6` | feat | EffectChainHost::descriptorForSlot+stateBlobForSlot |
| `d275e9e` | fix | slot accessors honor transient bypass flag (close race) |
| `de52591` | feat | StatefulSynthFixture (params + real clap_plugin_state) |
| `8606696` | refactor | drop unused cmath + harden StatefulSynthFixture snprintf |
| `c88d274` | feat | SessionSnapshot consults live host + repin/drift notifications |
| `cdcd7a0` | refactor | name notification buffer + surface state-hash in repin msg |
| `dde19fc` | feat | wire MainComponent slot lookup + scanner sink; close S2 followup |
| `3999f59` | feat | SIRIUS_THIRDPARTY_CLAP_PATH integration test |
| `4d65a5c` | fix | honest third-party-CLAP skip + named timeouts + leak-safe lifecycle |

### Quick reference for the next chat (M8 S2 test surface)

- **Full ctest:** 427 tests. Acceptable failures: the pre-existing M7
  SIGTERM flakes in `OutOfProcess*Tests` (subprocess timing race; 0-2
  cases/run; rerun confirms transient). 425/426 (GUI lifecycle) skip
  headlessly.
- **M8 S2 surface tags** (run via `build/tests/SiriusTests "[tag]"`,
  ctest `-R` matches by case NAME not tag): `[clap-bundle-loader]`,
  `[clap-scanner]`, `[plugin-state-region]`, `[plugin-state-extension]`,
  `[chain-host-state]`, `[stateful-synth-fixture]`, `[session-snapshot-live]`.
- **Operator eyes-on for CLAP** (per spec acceptance): open a real CLAP
  (FabFilter or any installed), change a parameter, save, quit, relaunch,
  open → no drift; edit the JSON to bump the persisted version → reopen →
  drift notification with 8-char hash prefixes in the history pane.
- **Third-party CLAP automated test:** `cmake -B build
  -DSIRIUS_THIRDPARTY_CLAP_PATH="/path/to/your.clap"` then the
  `[third-party-clap]` cases run; unset = silently absent.

---

## HISTORICAL — M8 S1 fully shipped at `8d9818f` (superseded 2026-05-19 — M8 S2 fully shipped)

> ## ✅ M8 S1 fully shipped end-to-end (orchestrator+subagents execution)
>
> M8 S1 (ArchivalMode + per-instance VersionPinning record) is on
> `origin/master` at HEAD `8d9818f`. 12 commits across 7 plan tasks
> + a final consolidation, all green from `efae938`..`8d9818f`.
> Clean rebuild succeeds, ctest 400/400 pass (+19 vs 381 baseline),
> codesign verified, test-s7 green.
>
> **What landed (high level):**
> - `core/`: pure C++ RFC-6234 `sha256Hex`, `ArchivalMode` enum
>   (default `VersionPinning`), `VersionPinningRecord` value type
>   with `matches()` (excludes oversamplingRate) + `operator==`,
>   `PluginDescriptor::version`, `EffectChainEntry::archivalMode` +
>   `persistedSnapshot`.
> - `persistence/`: full JSON round-trip for the new fields via
>   `archivalModeTo/FromString` + `versionPinningRecordTo/FromVar`
>   helpers; pre-M8 sessions load cleanly with defaults.
> - `engine/`: `SessionSnapshot` populator (copy-on-write walker,
>   lazy `std::optional<Constituent>` materialization, structural
>   sharing pinned by test) + verifier (formats compact drift
>   message, posts `Warning / PluginEvent` via existing bus).
> - `app/MainComponent.cpp`: populator wired into
>   `chooseFileAndSave`, verifier into `chooseFileAndLoad`.
> - Operator-visible: save session → hand-edit
>   `persistedSnapshot.version` in the JSON → reload → drift
>   message appears in the notification-history pane.
>
> **The single most important next move:** start M8 S2 (CLAP loader
> hardening + `clap_plugin_state` integration). Spec hasn't been
> written yet — run `superpowers:brainstorming` against V7 plan
> lines 597-598 first, then `superpowers:writing-plans`, then
> orchestrator+subagents.

### First moves for the next chat (concrete, ordered)

1. **Sanity:** `git status` clean, `git log --oneline -3` shows
   `8d9818f` on top.
2. **Read the M8 S2 followup entry in `todo.md`** (lines ~1485-1497).
   It enumerates 7 concrete sub-tasks the M8 S2 implementer must
   address, including two semantic gotchas surfaced by the M8 S1
   final review:
   - **Drop the `! has_value()` guard** in
     `engine/src/SessionSnapshot.cpp:40-41` once
     `OutOfProcessEffectChainHost::descriptorForSlot` /
     `stateBlobForSlot` exist. Without dropping it, the populator
     never refreshes a stale snapshot — a v1.0.0→v1.0.1 upgrade
     between save-A and save-B silently re-pins to the old version.
     Decide the re-pinning policy (always-refresh vs warn-then-
     refresh) before writing code.
   - **128-byte drift-message truncation budget** at
     `engine/src/SessionSnapshot.cpp:91`. Current format has ~75
     bytes of headroom; adding state-blob hash or any new field
     requires counting bytes or reordering so the most-actionable
     info survives truncation.
3. **Brainstorm M8 S2.** Spec doesn't exist yet — V7 plan lines
   597-598 only give the milestone-level intent. Use
   `superpowers:brainstorming` to produce
   `docs/superpowers/specs/2026-MM-DD-m8-s2-design.md`, then
   `superpowers:writing-plans` for the implementation plan.
4. **Execute via `superpowers:subagent-driven-development`**
   (matches V7's `orchestrator+subagents` mode for the M8 block).

### M8 S1 commit log (13 commits, all pushed)

| SHA | Type | Subject |
|---|---|---|
| `70c0f8f` | feat | pure C++ SHA-256 in core (RFC 6234) |
| `66c41dc` | feat | PluginDescriptor::version round-trips through SessionFormat |
| `0e64f67` | refactor | trim PluginDescriptor.version docstring + add round-trip test intent |
| `1412046` | docs | plan — fix Position constructor calls (single Rational, not two) |
| `3c56dc5` | feat | ArchivalMode enum (DeterminismContract/WetCapture/VersionPinning) |
| `9632241` | refactor | ArchivalMode test docstring matches actual enforcement |
| `df9682c` | feat | VersionPinningRecord value type with matches() semantics |
| `b2c78ff` | refactor | co-locate VersionPinningRecord comparators + add hash-drift test + tidy test includes |
| `dbd2b99` | refactor | drop unused VersionPinningRecord using-decl |
| `a0425aa` | feat | EffectChainEntry.archivalMode + persistedSnapshot + JSON round-trip |
| `21c9ea9` | feat | SessionSnapshot populator + verifier with PluginVersionDrift notifications |
| `05a557c` | refactor | defer Constituent copy in populator + pin structural-sharing invariant |
| `eda41a3` | refactor | drop unused INotificationSink using-decl |
| `47a74c1` | feat | wire populator/verifier into save+load; queue M8 S2 followup |
| `8d9818f` | refactor | re-save policy comment + WetCapture invariant test + S2 followup notes |

### Coverage gaps explicitly accepted at M8 S1 ship (not blockers)

The final review (operator-approved, recorded here for the M8 S2 implementer):

- **Multi-entry mixed-mode chain test missing.** Every populator/
  verifier test uses single-entry chains. The current implementation
  almost certainly handles `[VersionPinning, DeterminismContract,
  VersionPinning]` correctly (the loop in `walkAndPopulate` doesn't
  short-circuit), but it's not pinned. ~20-line test would close it;
  defer to M8 S2 since the same code is touched.
- **Multi-level grandchild verifier test missing.** `walkAndVerify`
  recurses through `node.children()` so deep trees should work, but
  the only multi-level test exercises the populator, not the
  verifier. Defensive; defer to M8 S2.

### Other queued items (independent of M8 progression)

- **Audio-ring SPSC violation** (M7 S5 deviation #1). RT-path
  discipline fix. Standalone.
- **Out-of-process `PluginScanner`** — wait until M20/M21 because
  the scanner re-architects around the same child binary.
- **`PluginScanner` "FabFilter" testing-filter removal** (line 38
  of `host/src/PluginScanner.cpp`). Hardcoded name filter from M7
  S7. Must go before shipping. Tracked in `todo.md`.
- **`get-task-allow=true` notarization blocker** —
  `app/CMakeLists.txt`'s Xcode attributes still inject
  `get-task-allow=true` on Release builds; notarization rejects
  this. Tracked in `todo.md`.

### Quick reference for the next chat (Sirius Looper specifics)

- **Run the headless lifecycle tests:** `bash bash/test-s7.sh`
  (auto-builds anything missing). Name still says s7 — covers all
  current MainComponentPluginEditorTests cases.
- **Run the M8 S1 surface tests:** `./build/tests/SiriusTests
  "[archival-mode]"` → 18 cases / 46 assertions. Or
  `[session-snapshot]` → 8 / 21. Or `[version-pinning]` → 5 / 11.
- **Full ctest:** 400 tests. The only acceptable failures are the
  pre-existing M7 SIGTERM flakes in
  `OutOfProcessPluginInstanceAudioThreadTests` /
  `OutOfProcessPluginTests` /
  `OutOfProcessEffectChainHostSupervisorTests` (subprocess timing
  race; flakes 0-2 cases per run; unrelated to M8 S1).
- **Bundle / signing / entitlements:** unchanged since M7 S9. The
  `sirius_plugin_host` child still has the
  `com.apple.security.cs.disable-library-validation` entitlement
  (`host_process/sirius_plugin_host.entitlements`).
- **M8 S1 spec:**
  `docs/superpowers/specs/2026-05-19-m8-s1-design.md`.
- **M8 S1 plan:**
  `docs/superpowers/plans/2026-05-19-m8-s1-plan.md` (7-task TDD
  decomposition; reflects the 2 plan-vs-spec deviations that
  landed: pure-C++ SHA-256 in core, host parameter dropped from
  populator/verifier signatures for M8 S1).

---

## HISTORICAL — M8 S1 spec committed; ready for writing-plans + implementation (superseded 2026-05-19 — M8 S1 fully shipped end-to-end)

> ## ✅ M7 S9 fully shipped + ✅ M8 S1 brainstorm + spec committed
>
> Two distinct outcomes from the 2026-05-19 session:
>
> 1. **M7 S9 eyes-on verified end-to-end** + a small library-validation
>    entitlement fix landed (see "What landed today" below). S9 is
>    closed; no follow-up work needed.
> 2. **M8 brainstorm completed.** The original session-end plan was
>    "S10 = pull VST3 + AU forward into M7"; the operator pivoted
>    mid-brainstorm to **honor V7 sequencing** and start M8 next per
>    `docs/superpowers/plans/2026-05-17-v7-alignment.md`. M8 S1 spec
>    is committed at HEAD `f73cf98` (pushed to origin/master).
>
> **The single most important first move for the next chat: read
> `docs/superpowers/specs/2026-05-19-m8-s1-design.md`, then invoke
> the `superpowers:writing-plans` skill on it.** The operator already
> approved the spec for proceed-to-implementation. Do not re-
> brainstorm. Do not re-explore the white paper. The spec is
> self-contained and references every relevant white paper section +
> V7-plan line. Just plan it and execute.

### First moves for the next chat (concrete, ordered)

1. **Sanity:** `git status` clean, `git log --oneline -3` shows
   `f73cf98` on top.
2. **Read the spec:**
   `docs/superpowers/specs/2026-05-19-m8-s1-design.md` (~370 lines).
   It's self-contained — the conversation that produced it is gone
   but the spec captures every decision.
3. **Invoke the `superpowers:writing-plans` skill** with the spec as
   input. The skill will produce
   `docs/superpowers/plans/2026-05-19-m8-s1-plan.md`. Operator
   reviews that plan before implementation starts.
4. **Implementation (separate session per V7 execution mode).**
   Per V7's M8 block (line 620): `orchestrator+subagents`. Expect
   parallelisable subtasks for the new files
   (`ArchivalMode.h`, `VersionPinningRecord.{h,cpp}`,
   `SessionSnapshot.{h,cpp}`, `Sha256.cpp`,
   `ArchivalModeTests.cpp`) + serial modifications to existing
   files (`PluginDescriptor.h`, `EffectChain.h`,
   `SessionFormat.cpp`, `PluginScanner.cpp`,
   `NotificationBus.cpp`).
5. **Acceptance per spec:** `ctest -R "ArchivalMode"` passes 4-6 new
   cases; existing 381-test ctest count grows by exactly that delta;
   `bash/test-s7.sh` lifecycle tests still pass; clean rebuild from
   `rm -rf build` succeeds.

### M8 S1 in one paragraph

`ArchivalMode` enum (DeterminismContract / WetCapture /
VersionPinning, default VersionPinning) lands on `EffectChainEntry`
alongside an `std::optional<VersionPinningRecord> persistedSnapshot`
field. The record is `{uniqueId, version, stateBlobSha256,
oversamplingRate, declaredInternalStateHash}` — the
portable + persistable identity of one hosted plug-in at the moment
of `serializeSession`. New `SessionSnapshot` helpers bridge the
live `OutOfProcessEffectChainHost` to the persistence layer at save
time (snapshot population) and at load time (drift verification +
`NotificationBus::PluginVersionDrift` emission). State blob hashing
is the SHA-256 of zero bytes for this session — real CLAP state
hashing arrives in M8 S2 with `clap_plugin_state` integration. No
CLAP work, no Constituent state machine, no WetCapture writer in
this session; each is its own later M8 session.

### Why M8 next (sequencing rationale)

The V7 plan
(`docs/superpowers/plans/2026-05-17-v7-alignment.md` lines 557-621)
puts these milestones in this order:

- **M7** — Out-of-process plug-in hosting framework (✅ shipped through S9)
- **M8** — Plug-in determinism + failure semantics + CLAP as first format (next)
- **M9-M19** — Modality completion + persistence + signal-flow surfaces
- **M20** — VST3 host (after CLAP/M8 is solid)
- **M21** — AU host (prep for iOS in M23)

The operator's library is mostly VST3/AU, which made "pull M20+M21
forward into M7 S10" tempting (and was the initial mid-2026-05-19
plan). The operator's final answer: *"Let's finish in logical
order. ... Follow the white paper — it is our golden rule and
source of truth to follow."* M8 ships next, then M9-M19 in order,
then VST3 (M20), then AU (M21). VST3/AU stay un-loadable in
production until M20/M21 land — accepted tradeoff for architectural
discipline.

### Routing concerns verified against the white paper

The operator flagged two preferences during the brainstorm; both are
already spec'd in the white paper and require no design rework:

1. **Plug-ins on input AND output mixers.** White paper §5.2
   (lines 352 & 354) treats both as "full creative mixers" — input
   mixer channels apply gain/EQ/dynamics/bus routing (line 352);
   output mixer channels apply channel-strip processing (line 354).
   Plug-ins live on both sides.
2. **Per-phrase output channels (not per-tape).** White paper §6.6
   (line 556): *"Per-Constituent channel strips. Each active
   Constituent — loop, phrase, section — gets its own channel
   strip."* The Constituent hierarchy is
   `tape → loop → phrase → section → song → set` (line 35); strips
   exist at the Constituent level, which includes phrases
   separately from their parent tape.

No box is being painted. The architecture is the operator's; we're
implementing in V7 order.

### What landed today (2026-05-19, both halves of the session)

| Commit | Subject |
|---|---|
| `46557e2` | fix: M7 S9 eyes-on — sirius_plugin_host needs com.apple.security.cs.disable-library-validation entitlement |
| `efda081` | docs: continue.md — M7 S9 fully shipped (eyes-on verified, library-validation entitlement landed) |
| `f73cf98` | docs: M8 S1 design — ArchivalMode enum + per-instance VersionPinning record |

All three on `origin/master`. Working tree was clean at end of
session.

### M7 S9 close-out (recently shipped; don't redo)

S9 is verified. Don't reopen it. Summary:

- Reaper-style separate plug-in windows: each `sirius_plugin_host`
  child owns its own top-level NSWindow per editor; cross-process
  pixel-transport scaffolding (XPC bridge, CARemoteLayer,
  PluginGuiBridge, OutOfProcessEditorView, `xpc_service/`) is
  **deleted** (-1252 LOC net).
- Library-validation entitlement
  (`host_process/sirius_plugin_host.entitlements` →
  `com.apple.security.cs.disable-library-validation = true`)
  added today so the hardened child can dlopen plug-ins not signed
  by Team `RR5DY39W4Q`.
- Bundle layout: `Contents/MacOS/` has only `Sirius Looper` +
  `sirius_plugin_host`. No `Contents/XPCServices/`, no
  `Contents/Library/LaunchAgents/`.
- 381-test ctest baseline holds; `codesign --verify --deep --strict`
  clean.

Eyes-on confirmed by operator end-to-end: child-owned NSWindow
appears with the synthetic plug-in's blue NSView rendering inside,
close-X works, reopen works, no orphan processes after .app exit,
stderr clean.

Full S9 detail (rewritten code, deleted files, four empirical
refutations of cross-process mach-port-transfer) lives in
`docs/superpowers/specs/2026-05-18-m7-s9-design.md` and in the
historical section below.

### Followups queued for after M8 S1 lands

M8 has 10 sessions broken out in the V7 plan (lines 597-603):

- **M8 S2** — CLAP loader hardening + `clap_plugin_state` real
  state-blob hashing (the synthetic-fixture empty-state path
  becomes a real-CLAP path).
- **M8 S3** — `Constituent::State` enum (`Valid | Broken |
  Invalid`) + render-as-silence semantics.
- **M8 S4-5** — `WetCapture` writer + `<channelId>.wet.tape`
  storage path.
- **M8 S6** — LMC calibration corruption recovery.
- **M8 S7-8** — Tape reachability scan for disk-full rotation.
- **M8 S9-10** — End-to-end integration.

After M8 closes, the V7 sequence is M9 (MIDI 2.0 / UMP), M10 (Mix
snapshots), M11 (Sirius Archive Format — replaces the JSON
SessionFormat), M12+ on out.

Other queued items (independent of M8 progression):

- **Audio-ring SPSC violation** (M7 S5 deviation #1). RT-path
  discipline fix. Standalone.
- **Out-of-process `PluginScanner`** — wait until M20/M21 because
  the scanner re-architects around the same child binary.
- **`PluginScanner` "FabFilter" testing-filter removal** (line 38
  of `host/src/PluginScanner.cpp`). Hardcoded name filter from M7
  S7 to shrink the operator's 1000+ plug-in scan during dev.
  Must go before shipping. Tracked in `todo.md`.
- **`get-task-allow=true` notarization blocker** —
  `app/CMakeLists.txt`'s Xcode attributes still inject
  `get-task-allow=true` on Release builds; notarization rejects
  this. Tracked in `todo.md`.
- **V7 plan reference cleanup** —
  `docs/superpowers/plans/2026-05-17-v7-alignment.md` may still
  mention `PluginGuiBridge` in M7's per-session block (deleted by
  S9). Worth a sweep when M9+ touches that file.

### Quick reference for the next chat (Sirius Looper specifics)

- **Run the headless lifecycle tests:** `bash bash/test-s7.sh`
  (auto-builds anything missing). Name still says s7 — covers all
  current MainComponentPluginEditorTests cases.
- **Launch the .app:** double-click
  `/Users/larryseyer/Desktop/Sirius Looper` (symlink) OR `open
  "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"`. For
  child-stderr capture during debugging, launch the binary directly:
  `"build/app/SiriusLooper_artefacts/Release/Sirius Looper.app/Contents/MacOS/Sirius Looper" 2>/tmp/sirius-app.log &`.
- **No `os_log` instrumentation exists in the host process.** The
  S8-era `os_log` calls were deleted with the XPC bridge in S9. The
  child writes diagnostics to `stderr` via `std::fprintf`. The
  `log stream` predicate
  `subsystem == "com.larryseyer.siriuslooper"` returns nothing; use
  direct-binary launch + stderr redirect instead.
- **Bundle path inside the .app:** `/Contents/MacOS/` has only
  `Sirius Looper` + `sirius_plugin_host`. No `Contents/XPCServices/`,
  no `Contents/Library/LaunchAgents/`.
- **`sirius_plugin_host` entitlements:**
  `host_process/sirius_plugin_host.entitlements` →
  `com.apple.security.cs.disable-library-validation = true`.
  Without this, the hardened child cannot dlopen any plug-in not
  signed by Team `RR5DY39W4Q`. Verify via
  `codesign -dv --entitlements - "$BUNDLE/Contents/MacOS/sirius_plugin_host"`.
- **`SessionFormat` JSON round-trip lives at
  `persistence/src/SessionFormat.cpp`.** M8 S1 extends
  `effectChainEntryToVar` / `effectChainEntryFromVar` (lines
  503-521) and `pluginDescriptorToVar` /
  `pluginDescriptorFromVar` (lines 481-501) — that's where the new
  field round-trips through.
- **SHA-256 already exists in `persistence/src/TapeStore.cpp`.** M8
  S1 factors it into a small `core/src/Sha256.cpp` so `core/` is no
  longer dependent on `persistence/` for hashing.
- **The `Constituent` is immutable + copy-on-write** (its
  `ChildPtr` is `shared_ptr<const Constituent>`). The
  `populateVersionPinningRecords` helper described in the spec must
  return a new tree, not mutate in place.

---

## HISTORICAL — M7 S9 close-out detail (verified eyes-on 2026-05-19; now in archived state, kept for archeology)

> S9 detail is below for archeological reference. The leading
> RESUME HERE block above has the operator-facing summary. Don't
> re-implement what's here.

### What M7 S9 actually changed (files + behaviour)

| File | Change |
|---|---|
| `host_process/gui_cocoa.mm` | **Rewritten.** Creates a top-level NSWindow per `sirius_gui_show`, attaches `SiriusPluginWindowDelegate` for close detection, makeKeyAndOrderFront. windowWillClose: publishes `responseContextId=0` back via shm. New C-linkage entry points: `sirius_appkit_init`, `sirius_appkit_drain_events`, `sirius_gui_set_state`. Manual retain/release (`-fno-objc-arc`). |
| `host_process/main.cpp` | Deleted `bootstrapXpcBridge()` (~130 LOC, the XPC client side). Added Apple-only `sirius_appkit_init()` call after parseArgs. Added per-iteration + onIdle calls to `sirius_appkit_drain_events()` so NSWindow events get dispatched. Wired `sirius_gui_set_state(guiState)` so the delegate can publish closes. |
| `host/src/OutOfProcessEffectChainHost.cpp` | Removed the lazy `PluginGuiBridge::instance()` touch in `configureBus`. The chain host no longer cares about the engine-side bridge. |
| `app/MainComponent.cpp` + `.h` | **Deleted `PluginEditorWindow`** (the engine-side `juce::DocumentWindow` per plug-in from S7). Replaced `std::vector<std::unique_ptr<PluginEditorWindow>>` with `std::vector<std::int64_t> openEditorBusIds_`. `openPluginEditor` now just calls `configureBus` + `requestEditorShow` — no engine-side window. `closePluginEditor` just `configureBus(empty)`. |
| `app/CMakeLists.txt` | Dropped the `sirius_gui_bridge` dependency + the `Contents/XPCServices/` copy POST_BUILD. The `sirius_plugin_host` Developer-ID re-sign + final outer-app re-sign survive. The 2026-05-19 follow-up added `--entitlements` to the child re-sign. |
| `host/CMakeLists.txt` | Dropped `PluginGuiBridge.cpp/.mm` and `OutOfProcessEditorView.cpp/.mm` from SiriusHost sources. Dropped `-framework QuartzCore` (no longer needed). |
| `host_process/CMakeLists.txt` | Dropped `-framework QuartzCore` (no longer needed in child either). |
| `CMakeLists.txt` (top-level) | Dropped `add_subdirectory(xpc_service)`. |
| `tests/CMakeLists.txt` | Dropped `PluginGuiBridgeTests.cpp` source + the Apple-only `CARemoteLayerRoundTripTests.mm` block. |
| `website/src/docs/whitepaper.md` | Lines 1435–1437 reworded: out-of-process GUI hosting now described as "each plug-in's editor appears as a separate floating window" (Reaper/Logic/Live convention). |
| `docs/superpowers/specs/2026-05-18-m7-s9-design.md` | Design doc capturing all four empirical refutations + the Reaper-style pivot. |

### Deleted by S9 (-1252 LOC)

- `xpc_service/` — entire directory (CMakeLists.txt, main.cpp, Info.plist.in)
- `host/include/sirius/PluginGuiBridge.h`
- `host/include/sirius/IGuiBridge.h`
- `host/include/sirius/OutOfProcessEditorView.h`
- `host/src/PluginGuiBridge.cpp`
- `host/src/PluginGuiBridge.mm`
- `host/src/OutOfProcessEditorView.cpp`
- `host/src/OutOfProcessEditorView.mm`
- `tests/PluginGuiBridgeTests.cpp`
- `tests/CARemoteLayerRoundTripTests.mm`

### S9 empirical refutations (documented for future archeology)

1. **`MachServices` in app `Info.plist`** — refuted. That key is
   parsed by launchd, not by LaunchServices; an app's Info.plist
   isn't a launchd .plist. `xpc_connection_create_mach_service`
   against an unregistered name returns a connection that errors
   out on first send (`XPC_ERROR_CONNECTION_INVALID`).
2. **socketpair + `SCM_RIGHTS` for mach send-rights** — refuted.
   `sendmsg` returns `errno=9 (EBADF)`. PoC at `/tmp/scm_test.c`.
   XNU's `bsd/kern/uipc_usrreq.c` validates each cmsg word as an
   fd-table entry; mach port names that aren't also valid fds are
   rejected.
3. **`xpc_endpoint_t` via inherited fd** — refuted. No public
   flat-byte serialization API; the object only survives transport
   as a value inside an existing `xpc_dictionary_t`.
4. **`NSXPCListenerEndpoint` via `NSKeyedArchiver`** — refuted by
   PoC at `/tmp/xpc_poc/`. Runtime exception: *"-[NSXPCListenerEndpoint
   encodeWithCoder:]: This class may only be encoded by an
   NSXPCCoder"*.

The only remaining no-entitlement cross-process mach-port-transfer
path on modern macOS is launchd-mediated bootstrap (SMAppService +
bundled LaunchAgent). After the four refutations, the operator
surfaced "why don't we follow Reaper?" — and the Reaper-style pivot
sidestepped the whole class of problems by letting each child own
its NSWindow directly.

---

## HISTORICAL — M7 S8 partial-win + M7 S9 plan + S9 initial commit (superseded 2026-05-19 — M7 S9 now fully shipped after eyes-on + entitlement fix)

> Original S8 close + S9 plan below: S8 unblocked the engine→bridge handshake but child→bridge was architecturally blocked. The S9 plan picked "engine-as-XPC-server" but every elegant variant failed empirically; the final shipped architecture (Reaper-style separate windows) is documented above.

---

## RESUME HERE (2026-05-18 — M7 S8 partial-win on origin/master; M7 S9 = bridge architecture rework)

> ## ✅ M7 S8 ENGINE PATH GREEN / 🛑 CHILD PATH BLOCKED (S9 = pick a new bridge architecture)
>
> S8 unblocked the **engine→bridge handshake**: signing fix (Developer
> ID + identifier match + Info.plist binding + parent re-seal) plus
> the correct XPC API (`xpc_connection_create`, not
> `xpc_connection_create_mach_service`). os_log trace at 21:59:32:
> ```
> engine: sending set_server_port
> sirius_gui_bridge: xpc_main starting
> service: op=set_server_port
> engine: set_server_port ok; ready_=true
> ```
> But the **child→bridge handshake hits an architectural wall**: in-app
> XPC services in `Contents/XPCServices/` are PRIVATE per-process.
> launchd spawns a SEPARATE bridge instance for the child, with empty
> state. The "shared broker" pattern doesn't work via in-app XPC. The
> operator-visible failed-to-load overlay still shows because the child
> can't fetch the engine's serverPort. **S9 picks a new bridge
> architecture** — LaunchAgent install / engine-as-XPC-server /
> anonymous-endpoint handoff / socketpair-with-SCM_RIGHTS. Each option
> + tradeoffs is enumerated in `todo.md` (the new M7 S8 partial-win
> entry near the top). Recommended pick per "professional and elegant":
> **engine-as-XPC-server** (eliminates the bridge process entirely).
> Confirm before implementing.

### What M7 S8 actually changed (files + behaviour)

Five concrete edits, plus 5 commits (instrumentation, codesign,
docs). Test count: **387/387** ctest still green (no regressions);
`codesign --verify --deep --strict` on the dev `.app` now passes.

| File | Change |
|---|---|
| `xpc_service/main.cpp` | `os_log` instrumentation throughout (`subsystem="com.larryseyer.siriuslooper"`, `category="gui-bridge"`); error-path logging with `XPC_ERROR_KEY_DESCRIPTION`. |
| `host/src/PluginGuiBridge.cpp` | `os_log` instrumentation + switched `xpc_connection_create_mach_service` → `xpc_connection_create` (correct API for in-app XPC services). |
| `host_process/main.cpp` | `os_log` instrumentation + same API switch. Note: child path still doesn't deliver — see "still broken" below. |
| `xpc_service/CMakeLists.txt` | POST_BUILD `codesign --force --sign "Developer ID Application: Larry Seyer (RR5DY39W4Q)" --identifier "com.larryseyer.siriuslooper.gui-bridge" --options runtime --generate-entitlement-der`. Ad-hoc was AMFI-rejected (Code=-423). The `--identifier` flag is critical — without it, codesign defaults to the binary name, mismatching CFBundleIdentifier and breaking launchd registration. |
| `app/CMakeLists.txt` | Two new POST_BUILDs: (a) Developer-ID re-sign of `sirius_plugin_host` after copy into `Contents/MacOS/`, (b) final Developer-ID re-sign of the parent `.app` AFTER all helper copies — NO `--deep` (that would clobber inner identifiers). Closes M7 S6 deviation #2 (re-sign gap). |

**What works now (engine side):**

- Engine's `XpcGuiBridge` ctor opens a connection to
  `com.larryseyer.siriuslooper.gui-bridge` via `xpc_connection_create`.
- launchd resolves the in-app bundle scope, spawns the .xpc binary
  with a Developer-ID-trusted signature (AMFI accepts).
- Bridge receives `set_server_port`, caches the port, replies `{"ok": true}`.
- Engine's `ready_` atomic flips to true; `PluginGuiBridge::instance().isReady()` returns true.
- `OutOfProcessEditorView`'s "bogus contextId" check now correctly
  distinguishes real-vs-placeholder publish from the child.

**What's still broken (child side, ARCHITECTURAL):**

- Child's `bootstrapXpcBridge()` opens its OWN connection to
  `com.larryseyer.siriuslooper.gui-bridge`. launchd spawns a **separate**
  bridge process in the child's pid domain, with empty cached state.
- Child's `get_server_port` returns no port. `g_engineServerPort` stays
  `MACH_PORT_NULL`. `gui_cocoa.mm` falls back to the placeholder
  counter contextId. Operator-visible failed-to-load overlay still
  fires.
- **Root cause**: in-app XPC services in `Contents/XPCServices/` are
  PRIVATE per-calling-process. The S6 design assumed they could
  function as a shared broker across unrelated processes (engine +
  child); they can't. This is an Apple-API constraint, not a code bug.

### First moves for the M7 S9 chat

S9 = pick a new bridge architecture. Four options enumerated in
`todo.md` (the 2026-05-18 M7 S8 partial-win entry):

1. **LaunchAgent install** — bridge becomes a launchd-managed user-
   session service; `.plist` in `~/Library/LaunchAgents/`; switch
   back to `xpc_connection_create_mach_service`. Minimal code; needs
   one-time operator install.
2. **Engine as XPC server (RECOMMENDED)** — eliminates the bridge
   process. Engine vends a Mach service listener; child connects by
   name. No separate broker, no .plist, no install. Slightly more
   engine-side code.
3. **Anonymous endpoint handoff** — engine creates the bridge
   connection in-process, serializes an `xpc_endpoint_t`, passes to
   child via spawn-time env var; child reconstructs via
   `xpc_connection_create_from_endpoint`. Keeps the bridge process.
4. **socketpair + SCM_RIGHTS** — skip XPC entirely. Engine and child
   share a UNIX socketpair; engine sends the Mach send-right via
   `cmsghdr(SCM_RIGHTS)`. Largest code change. Documented in
   `docs/superpowers/specs/2026-05-18-m7-s6-design.md` as the
   fallback.

**Recommended pick: Option 2 (engine as XPC server)** per the
operator's "default to most professional and elegant" rule —
eliminates an entire process from the deployment surface; no install
step; one source of truth. Confirm with the operator before
implementing.

**Reproduce the partial-win:**

```
# Clean build + launch
rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target SiriusLooper

# Watch the os_log live during operator click
log stream --predicate 'subsystem == "com.larryseyer.siriuslooper"' &
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
# Operator: click "Plugins" tab → "Open synthetic test plug-in (debug)"

# Expected log: engine handshake succeeds (`set_server_port ok`), child
# spawns its own bridge (`pid/<child_pid>/...gui-bridge`), child's
# get_server_port returns no port, child logs `bootstrapXpcBridge ok
# (serverPort=0)`, editor window shows failed-to-load overlay.
```

---

**HISTORICAL — M7 S7 close + M7 S8 handoff (superseded 2026-05-18 — M7 S8 now partially shipped)**

**M7 S7 is on `origin/master`** + 9 eyes-on follow-up commits also
pushed. Current `origin/master` HEAD before S8 was `77b8012`. S7
feature-set close-out was at `1c52a36`; everything `1c52a36..5f384a9`
is **post-S7 eyes-on session work** (UX bug fixes + diagnostic
tooling + operator-facing buttons + the XPC-bridge-unreachable
discovery that S8 partially fixed).

### Original S7 feature commits (1c6301e..1c52a36)

11 per-step commits + 1 close-out, all pushed. Per-step SHAs:

| SHA | Subject |
|---|---|
| `eec63e2` | feat: M7 S7 step 1 — MainComponent owns OutOfProcessEffectChainHost + hostBinaryPath helper |
| `42dbc73` | feat: M7 S7 step 2 — MainComponent::openPluginEditor / closePluginEditor lifecycle (stub window) |
| `10762ec` | feat: M7 S7 step 3 — PluginsPane uses ListBox with per-row Open-editor buttons |
| `e734014` | fix: M7 S7 step 3 review — failedFilesLabel layout fix + drop dead RowButton state + clean stale comment |
| `ba86407` | feat: M7 S7 step 4 — PluginEditorWindow (juce::DocumentWindow) owns OutOfProcessEditorView + busId-targeted close |
| `7841c40` | refactor: M7 S7 step 4 — move OutOfProcessEditorView.h include from .h to .cpp (only PluginEditorWindow body needs it) |
| `269df34` | docs: M7 S7 step 4 review — soften PluginEditorWindow comment (view rebinds itself; doesn't resize parent) |
| `e231e22` | feat: M7 S7 step 5 — wire PluginListBox Open-editor button → openPluginEditor + host-binary availability |
| `3f70af4` | test: M7 S7 step 6 — MainComponent plug-in editor lifecycle tests + eyes-on doc refresh |
| `c753488` | fix: M7 S7 step 6 review — Test 2 uses firstOpenBusIdForTesting (drops magic 1000) |

Test count: **387/387** green on clean rebuild (was 384 at S6; +3 in
S7 — three `[main-component-plugin-editor]` lifecycle cases: ctor/dtor
sanity always runs; the two fixture-dependent cases SKIP cleanly when
`hostBinaryPath()` isn't resolvable from the test binary location AND
run/pass when the .app bundle is in scope).

**S7 made S6's cross-process compositing operator-visible.** The
Plugins tab now shows each scanned descriptor in a `juce::ListBox`
with a per-row "Open editor" button. Clicking spawns a
`sirius_plugin_host` child on a scratch busId (1000+) and floats a
`juce::DocumentWindow` containing an `OutOfProcessEditorView`. The
close-button tears down the slot via
`juce::MessageManager::callAsync` (avoids destroying the window from
inside its own JUCE event-dispatcher callback). Multiple plug-ins
loadable simultaneously. No audio routes through the scratch chains —
engine output uninterrupted.

**Architectural shape (carried forward):**

- **`MainComponent` owns the `OutOfProcessEffectChainHost`.** Member
  declaration order is `effectChainHost_` BEFORE `editorWindows_` so
  windows destruct first (reverse-declaration order) and the chain
  host outlives them — each window's `OutOfProcessEditorView` polls
  the host at 30 Hz.
- **`MainComponent::PluginEditorWindow`** — new `juce::DocumentWindow`
  subclass, ~40 LOC. Native title bar, resizable, `allButtons`. Holds
  `busId_` + a `std::function<void(std::int64_t)> onClose_` callback
  that captures `this`-equivalent state by value before
  `MessageManager::callAsync` dispatches the teardown.
- **`MainComponent::PluginListBox`** — new
  `juce::ListBoxModel` subclass, ~110 LOC. Sibling of `PluginsPane`
  (not deeply nested — avoids fragile nested-nested forward
  declarations). Each row paints descriptor info + a "Open editor"
  `juce::TextButton` via `refreshComponentForRow`. Tooltip on
  disabled state explains "launch the .app for plug-in hosting."
- **Test-only accessors** on `MainComponent` (`hostBinaryPathForTesting`,
  `editorWindowCountForTesting`, `firstOpenBusIdForTesting`,
  `childPidForTestingAtBusId`, `openPluginEditorForTesting`,
  `closePluginEditorForTesting`). Public, marked `*ForTesting`,
  clearly documented as not part of the operator surface.
- **`MainComponent::PluginEditorWindow` separate test executable.**
  Because `app/MainComponent.cpp` isn't compiled into any library
  (only into SiriusLooper.app), the new test exe re-compiles it with
  the same link set. Apple-only `if(APPLE)` guard.

**Measured: app launch + quit cycle clean** — no orphan
`sirius_plugin_host` or `sirius_gui_bridge` processes after
`osascript -e 'quit app "Sirius Looper"'`. ctest 387/387 green; RT
audio surface untouched (no S7 code reaches the audio thread).

### Scope deviations locked in S7 (carry forward to S8)

1. **`PluginListBox` is a sibling of `PluginsPane`** (not deeply
   nested inside it). The plan offered both placements; sibling won
   for fragility-of-forward-decl reasons.
2. **`OutOfProcessEditorView.h` lives in
   `app/MainComponent.cpp`'s includes, NOT in `MainComponent.h`.**
   The header only forward-declares `PluginEditorWindow`; the type
   `sirius::OutOfProcessEditorView` is only referenced inside
   `PluginEditorWindow`'s definition (in the .cpp). Include-what-you-
   use applied.
3. **Plan corrections found during execution:**
   - Plan said `sirius::PluginFormat::CLAP` — actual enum is `Clap`
     (lowercase 'l'). Adapted in `tests/MainComponentPluginEditorTests.cpp`.
   - Plan said `MainComponent component (dm);` — actual ctor takes
     no arguments (the component owns its own `juce::AudioDeviceManager`).
     Adapted.
4. **`failedFilesLabel_` first-paint height bug** (caught in step 3
   review). `PluginsPane::setDescriptors` now calls `resized()` after
   setting the label's text, so the failed-files strip appears
   immediately instead of waiting for the next parent resize.
5. **`PluginEditorWindow` doesn't resize itself to the plug-in's
   preferred size.** The embedded `OutOfProcessEditorView` rebinds
   its own bounds to the child's published CARemoteLayer contextId,
   but doesn't (yet) propagate up to the parent `DocumentWindow`.
   Initial 600×400; operator resizes manually. Worth a later session.
6. **`(#N)` suffix for duplicate-descriptor window titles NOT
   implemented.** Opening the same plug-in twice produces two windows
   with identical titles. Acceptable for S7 eyes-on; minor polish for
   later.
7. **Pre-existing timing-flaky tests** (#247 "concurrent producer +
   consumer", #252 "permanent bypass") flaked once each under full-
   suite contention during S7 verification. Both pass in isolation.
   Not S7-introduced.

### Post-S7 eyes-on session (1c52a36..5f384a9 — 9 commits, all pushed)

The eyes-on session that followed S7 close-out uncovered FOUR things,
in this order. All four are on `origin/master`.

| SHA | Subject |
|---|---|
| `13f7b01` | fix: M7 S7 final review — wire setNotificationSink + explicit editorWindows_.clear() in dtor |
| `f916e37` | fix: M7 S7 — PluginScanner testing-only FabFilter filter to skip per-file instantiation on 1000+ plug-in installs |
| `4734fb7` | chore: M7 S7 — bash/test-s7.sh wraps the lifecycle tests against synthetic CLAP fixture |
| `98f93db` | feat: M7 S7 — OutOfProcessEditorView paints failed-to-load overlay after 2s timeout (was blank black for VST3/AU) |
| `909ed74` | feat: M7 S7 — Plugins-tab quick-scan buttons (global + user) + synthetic CLAP debug button |
| `9319977` | fix: M7 S7 — hide opaque NSViewComponent in failed-to-load state so paint() overlay actually reaches the screen |
| `7321c85` | fix: M7 S7 — issueShowIfSized retry on first resize + always-stamp showRequestedAt so failed-to-load fires (was deadlocked on bounds<=0 at parentHierarchyChanged) |
| `d034e79` | fix: M7 S7 — failed-to-load deadline runs from ctor, not from Show success (decoupled from shownOnce_ lifecycle uncertainty) |
| `5f384a9` | fix: M7 S7 — OutOfProcessEditorView detects bogus placeholder-counter contextId via PluginGuiBridge::isReady (overlay was flashing + never fired before) |

**Four discoveries:**

1. **`sirius_plugin_host` only loads `.clap` bundles.** VST3/AU are
   silently rejected (the child can't dlopen them). Operator's 1000+
   installed plug-ins are mostly VST3 + AU; they can't be hosted as-
   is. **This is an architectural gap, not a bug** — a separate later
   session adds VST3/AU support to the child via JUCE's
   `AudioPluginFormatManager`.
2. **`PluginScanner` doesn't register CLAP** (header comment at
   `host/include/sirius/PluginScanner.h:29-32` is explicit). So even
   if you DO have a CLAP installed, it's invisible to the scan UI.
   The synthetic test plug-in is reachable only via the new debug
   button (commit `909ed74`).
3. **The "blank black window on failure" UX hole.** S7 left the
   embedded NSView opaque even when nothing was published. Fixed
   across commits `98f93db` + `9319977` + `7321c85` + `d034e79` +
   `5f384a9` — operator now gets a "Plug-in failed to load" overlay
   after 2 s when contextId is either missing OR bogus (see #4).
4. **THE BIG ONE: the XPC bridge is unreachable from the .app
   context.** When the engine constructs `PluginGuiBridge::instance()`,
   the XPC connection to the bundled
   `com.larryseyer.siriuslooper.gui-bridge` service NEVER receives
   the `{"ok": true}` reply. `isReady()` returns false forever. The
   child's `bootstrapXpcBridge` also times out (250 ms). Result:
   even the synthetic CLAP plug-in renders the failed-to-load
   overlay because the engine correctly treats the child's
   placeholder-counter contextId as bogus. **This is the root cause
   blocking real cross-process pixels via CARemoteLayer. It must be
   S8.** Tracked in `todo.md` (last entry).

**Other operator-facing changes from the eyes-on session:**

- **`PluginScanner` hardcoded "FabFilter" name filter** (commit
  `f916e37`). The operator has 1000+ plug-ins; a full scan was
  taking minutes. Skips files whose path doesn't contain "FabFilter"
  before per-file instantiation. **REMOVE THIS BEFORE SHIPPING** —
  tracked in `todo.md`.
- **`bash/test-s7.sh`** — wraps the headless lifecycle tests
  against the synthetic CLAP fixture. Run any time to verify the
  S7 chain works without a GUI.
- **Plugins tab additions** (commit `909ed74`): two quick-scan
  buttons (`/Library/Audio/Plug-Ins` global, `~/Library/Audio/Plug-Ins`
  user) + an "Open synthetic test plug-in (debug)" button that
  bypasses the scanner.
- **Desktop alias** at `/Users/larryseyer/Desktop/Sirius Looper`
  → `build/app/SiriusLooper_artefacts/Release/Sirius Looper.app`
  (symlink). Operator double-clicks to launch.

### First moves for the M7 S8 chat

**S8 = XPC bridge debug.** This is the single most important
remaining M7 work — without it, none of S6's CARemoteLayer pixel
compositing actually paints in the .app eyes-on. The plumbing
through every other layer (S3 audio path, S4 supervisor, S5
PluginGuiState IPC, S7 MainComponent UI) is proven working; only
the cross-process CARemoteLayer handshake is broken.

**Where to start the investigation:**

1. **Read `todo.md`'s last entry** — it lists the likely candidates
   (signature mismatch / `XPCService.ServiceType` value / launchd
   not registering the service at all). Use that as the working
   hypothesis list.
2. **Confirm whether launchd sees the bundled .xpc** —
   `launchctl print user/$UID | grep gui-bridge` after the .app is
   launched. If nothing returned, launchd never registered the
   service.
3. **Capture the bridge binary's stderr** — modify
   `xpc_service/main.cpp` to write a marker to a known file at
   startup. Run the .app and check if the marker file appears. If
   not, the bridge binary was never executed.
4. **Verify ad-hoc signing parity** — `codesign -dvv` on
   `Contents/MacOS/Sirius Looper` AND
   `Contents/XPCServices/sirius_gui_bridge.xpc/Contents/MacOS/sirius_gui_bridge`.
   Same team ID? Same signing identity? Modern macOS requires
   embedded helpers to have the same signing team as the parent or
   launchd refuses to launch them.
5. **Check `XPCService.ServiceType`** — currently `Application`
   (per `xpc_service/Info.plist.in:23`). The "right" value for an
   in-app XPC service is debated; Apple's modern recommendation is
   often unset (use `Standard` semantics). Try removing the key
   entirely.
6. **Cross-check with the bundle re-sign gap** —
   `docs/operator/macos-sandbox.md` documents that the parent .app
   isn't re-signed after the helper copies. That gap may itself be
   the reason launchd refuses to trust the helper.

**If S8 unblocks the bridge:** the failed-to-load overlay will stop
firing, and the operator will see the synthetic plug-in's actual
blue NSView rendered cross-process inside the engine's window. Real
end-to-end proof of S6's CARemoteLayer work.

**If S8 can't unblock the bridge in a reasonable session:** fall
back to a non-XPC Mach-port handoff (the spec's alternative path —
anonymous Mach-port pair via UNIX socketpair with port descriptor,
documented in `docs/superpowers/specs/2026-05-18-m7-s6-design.md`
under "operator decisions locked").

### Other M7 candidates that did NOT make S8

S8 is XPC-bridge-debug; everything below is queued behind it. The
order changed materially because the eyes-on found a hard blocker.

1. **Audio-ring SPSC violation fix** (S5 deviation #1) — was the
   pre-eyes-on S8 candidate. Still important; demoted because
   nobody can open an editor mid-playback until the bridge works.
   Promote back to S9 unless S8 grows in scope.
2. **VST3/AU hosting in `sirius_plugin_host`** — newly important
   given the operator's installed library is mostly VST3/AU. Needs
   JUCE's `AudioPluginFormatManager` in the child binary.
3. **Re-enable full PluginScanner scan** — remove the hardcoded
   "FabFilter" filter once S8 + VST3 hosting both land.
4. **`PluginEditorWindow` resize-to-preferred-size** — small UX
   polish (~10 LOC). S7 deviation #5.
5. **Bundle re-sign for distribution** (S6 deviation #2 +
   `docs/operator/macos-sandbox.md`) — may merge into S8 if it
   turns out to be the root cause.
6. **Windows + Linux GUI embedding** — independent later sessions.

### S7-era decisions locked (S8 must preserve — superset of S6 list)

(Carry forward all S5/S6 items unchanged. New items:)

22. **Scratch bus IDs for editor-only loads** (1000+). The
    OutputMixer doesn't know about these buses; the chain host's
    `pumpSlot` is never called for them. Real per-bus chain
    integration is a later session.
23. **`MainComponent` owns the `OutOfProcessEffectChainHost`.** Lives
    for the main window's lifetime. `editorWindows_` declared AFTER
    the host so windows destruct first (members destruct in reverse
    declaration order).
24. **Floating `juce::DocumentWindow` per loaded plug-in** + multi-
    load. `closeButtonPressed` schedules teardown via
    `juce::MessageManager::callAsync` to avoid use-after-free in the
    JUCE event dispatcher.
25. **Test-only accessors are public + `*ForTesting`-suffixed.**
    Matches existing `OutOfProcessEffectChainHost::childPidForTestingAtSlot`
    convention. NOT `friend class`-based.
26. **MainComponent's plug-in tests live in their own test
    executable** because `app/MainComponent.cpp` isn't in any
    library. The exe re-compiles MainComponent.cpp with SiriusLooper's
    link set.
27. **`OutOfProcessEditorView` failed-to-load UX** (added during
    eyes-on session). After 2 s with no `currentContextId_`
    publish OR with a placeholder-counter contextId (detected via
    `! PluginGuiBridge::instance().isReady()`), paint a "Plug-in
    failed to load" overlay over a dark grey background. Hide
    `embed_` NSViewComponent at the same time (it's opaque even
    with no NSView set — would render black over our paint).
    Restore `embed_` visibility on a real late publish (not on the
    placeholder counter — that would flash the overlay on/off).
    The deadline runs from view ctor time, not from a Show success,
    so the timeout always fires regardless of upstream lifecycle
    uncertainty.
28. **Bridge-availability check is engine-side, not in the IPC
    contract.** The `PluginGuiBridge::instance().isReady()` query
    is local to the engine — it tells us if the engine's own XPC
    connection got `{"ok": true}` back. If yes, real CARemoteLayer
    pixels expected; if no, the child's contextId (if any) is the
    placeholder counter and `+[CALayer layerWithRemoteClientId:]`
    would render black. We did NOT add a "bridge available" bool to
    the `PluginGuiState` shm IPC; that would change the wire format.
    S8 may revisit this if it helps debug.

### Carryover NOT resolved (S8 doesn't touch unless flagged)

- **XPC bridge unreachable from .app context** — the new top
  blocker; IS S8. See "First moves" above + `todo.md` last entry.
- **`sirius_plugin_host` only handles CLAP** — operator's library
  is mostly VST3/AU; child can't dlopen them. Tracked above.
- **`PluginScanner` doesn't register CLAP** + currently hardcoded
  "FabFilter" filter for testing (commit `f916e37`). Both noted in
  `todo.md`. Remove the filter before any real shipping; add CLAP
  scanner registration when a `juce_clap` wrapper lands.
- **Audio-ring SPSC violation** (S5 deviation #1) — was S8, now
  S9 behind the bridge fix.
- **Bundle re-sign for distribution + `sirius_plugin_host`
  MACOSX_BUNDLE conversion** — `docs/operator/macos-sandbox.md`.
  May merge into S8 (might be the bridge-unreachable root cause).
- **`PluginEditorWindow` doesn't resize-to-preferred-size** (S7
  deviation #5).
- **`(#N)` suffix for duplicate-descriptor window titles** (S7
  deviation #6).
- **`PluginIpcMessage::monotonicNs` LMC reinterpret** still pending
  the audio-thread LMC handle.
- **Pre-existing timing-flaky tests** (#247, #252).
- **Carryover from M6 + earlier still unresolved** (ProcessedRoute
  empty span, manual operator-launch eyes-on of M6 surface, CI
  signing handoff) — unchanged from S5/S6/S7-era state.

### Quick reference for the next chat

- **Run the headless lifecycle test:** `bash bash/test-s7.sh`
  (auto-builds anything missing; passes 3/3 against synthetic CLAP).
- **Launch the .app:** double-click
  `/Users/larryseyer/Desktop/Sirius Looper` (symlink) OR `open
  "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"`.
- **Capture stderr from the .app:** `"build/app/SiriusLooper_artefacts/Release/Sirius Looper.app/Contents/MacOS/Sirius Looper" 2> /tmp/sirius-stderr.log &`
- **Read sirius_plugin_host logs:** `/usr/bin/log show
  --predicate 'process == "sirius_plugin_host"' --last 5m`
- **Read sirius_gui_bridge logs (S8 will need these):**
  `/usr/bin/log show --predicate 'process == "sirius_gui_bridge"'
  --last 5m`

---

## HISTORICAL — M7 S6 close + M7 S7 handoff (superseded 2026-05-18 — M7 S7 now shipped)

## RESUME HERE (2026-05-18 — M7 S6 on origin/master; M7 S7 selection next)

**M7 S6 is on `origin/master`.** S6 head is `e9f6220` (the close-out
docs commit); the last feature/docs-per-step commit is `10560c9`. 19
per-step commits + 1 close-out, all pushed. Per-step SHAs:

| SHA | Subject |
|---|---|
| `0b1d9b8` | feat: M7 S6 step 1 — IGuiBridge port + PluginGuiBridge null skeleton + unit tests |
| `3455436` | docs: M7 S6 step 1 review — tighten PluginGuiBridge docstrings (reference lifetime + resetForTesting reality) |
| `739e4ea` | feat: M7 S6 step 2 — sirius_gui_bridge XPC service binary + bundle |
| `095cfec` | fix: M7 S6 step 2 — drop unused includes from xpc_service/main.cpp |
| `9f37b8f` | fix: M7 S6 step 2 review — fail loud on missing op (reply with error instead of silent hang) |
| `fcc83dc` | feat: M7 S6 step 3 — PluginGuiBridge real XPC connection + CARemoteLayerServer.sharedServer.serverPort registration |
| `106e4e1` | fix: M7 S6 step 3 review — drain XPC handlers + release dispatch_queue in XpcGuiBridge dtor |
| `fb870e9` | feat: M7 S6 step 4 — install sirius_gui_bridge.xpc + sirius_plugin_host into app bundle |
| `aa64394` | refactor: M7 S6 step 4 review — collapse add_dependencies + note re-sign gap for distribution |
| `5faa18f` | feat: M7 S6 step 5 — OutOfProcessEffectChainHost lazy-touches PluginGuiBridge on configureBus |
| `7a20065` | feat: M7 S6 step 6 — child XPC bootstrap fetches engine serverPort with 250 ms timeout |
| `bc4c706` | fix: M7 S6 step 6 review — gate XPC bootstrap on clap mode + plug late-reply send-right leak |
| `2f20033` | feat: M7 S6 step 7 — gui_cocoa.mm publishes CARemoteLayerClient.clientId when serverPort available |
| `33861a9` | docs: M7 S6 step 7 review — refresh gui_cocoa.mm docblock to lead with CARemoteLayer (placeholder is now fallback) |
| `772cef0` | feat: M7 S6 step 8 — OutOfProcessEditorView.mm uses +layerWithRemoteClientId: when contextId is real |
| `f191954` | fix: M7 S6 step 8 review — autorelease NSView, extract placeholder-tint helper, drop unused CARemoteLayerServer.h import |
| `7ed65de` | test: M7 S6 step 9 — CARemoteLayer in-process server/client round-trip |
| `8042018` | fix: M7 S6 step 9 review — drop dead non-Apple SKIP branch (CMake already gates on APPLE) |
| `10560c9` | docs: M7 S6 step 11 — operator eyes-on procedure + future sandbox + signing-pipeline diff |

(One per-task push by a subagent during step 2 leaked the early
commits to origin out of plan — `739e4ea` and its review fixes were
on origin from step 2 onward, before the rest of S6 was visible. The
remaining steps were committed locally; the final `git push origin
master` at session end pushed `739e4ea..e9f6220`. Subsequent task
subagents were explicitly told "commit only, no push" and complied.)

Test count: **384/384** green on clean rebuild (was 378 at S5; +6 in
S6 — three `[plugin-editor-xpc][unit]` PluginGuiBridge cases + three
`[plugin-editor-xpc][in-process]` CARemoteLayer round-trip cases).

**S6 made cross-process GPU compositing real.** Engine's
`CARemoteLayerServer.sharedServer.serverPort` is brokered to each
`sirius_plugin_host` child via a bundled XPC service
(`Contents/XPCServices/sirius_gui_bridge.xpc/` — `CFBundleIdentifier
com.larryseyer.siriuslooper.gui-bridge`). Children construct
`CARemoteLayerClient`, publish the `clientId` via the existing S5
`PluginGuiState` shm, and the engine wraps `+[CALayer
layerWithRemoteClientId:]` inside the same `OutOfProcessEditorView`
shell from S5. Soft-fails to the S5 tinted placeholder when the bridge
bundle is missing (ctest, no-bundle dev runs).

**Measured RT (Apple Silicon, 2026-05-18):** `[plugin-ipc][.rt-smoke]`
median 66 µs, p99 135 µs, max 155 µs (S5 was 71 / 139 / 141).
Median + p99 improved slightly; max is +14 µs but well inside the
300 µs p99 ceiling. Audio surface unchanged (XPC is message-thread
only); drift is run-to-run noise.

**Architectural shape (carried forward):**

- **`xpc_service/sirius_gui_bridge`** — new tiny XPC service binary
  (~110 LOC after cleanup). JUCE-free, AppKit-free; only libSystem
  XPC + Mach + dispatch. Two ops: `set_server_port` (engine →
  bridge) and `get_server_port` (child → bridge). Stateless Mach-
  port broker; caches the most-recently-registered serverPort and
  hands a fresh send-right to each requesting child.
- **`IGuiBridge` port + `PluginGuiBridge` singleton** in `host/`.
  Lazy first-use construction; XPC connection + serverPort
  registration happen on the first `configureBus`. Per the S3/S4
  port pattern (`IEffectChainHost`, `INotificationSink`),
  `IGuiBridge` is the third dependency-inverted engine→host surface.
- **`PluginGuiBridge.cpp` + `.mm`** split — Cocoa work is in the
  .mm; the .cpp holds the XPC connection lifecycle. ARC default in
  the .mm; .cpp uses `dispatch_release` directly (verified
  available in plain C++ TUs on this SDK).
- **Child XPC bootstrap** in `host_process/main.cpp` —
  `bootstrapXpcBridge()` runs ONLY in `--mode clap` (skipped for
  identity-mode tests to avoid the 250 ms timeout per spawn).
  `dispatch_semaphore` + mutex-protected `consumed` flag handles
  the late-reply race so a slow bridge doesn't leak the
  send-right.
- **Bundle layout** — both `sirius_plugin_host` and the .xpc are
  copied into `SiriusLooper.app/Contents/` via POST_BUILD commands
  in `app/CMakeLists.txt`. Dev-loop ad-hoc signing works; Developer-
  ID distribution requires the re-sign step documented in
  `docs/operator/macos-sandbox.md` (operator-pending CI signing
  session).

### Scope deviations locked in S6 (carry forward to S7)

1. **XPC bundle OUTPUT_NAME deviates from plan-as-written.** Plan
   said `OUTPUT_NAME = "com.larryseyer.siriuslooper.gui-bridge"`
   which would have made the binary
   `Contents/MacOS/com.larryseyer.siriuslooper.gui-bridge` —
   contradicting `CFBundleExecutable = sirius_gui_bridge` in the
   plist. Plan had a self-inconsistency. The actual binary is
   `sirius_gui_bridge` and the .xpc directory is
   `sirius_gui_bridge.xpc`. launchd lookups are by
   CFBundleIdentifier so the directory name doesn't matter.
2. **Bundle re-sign for distribution NOT implemented.** The Xcode
   generator signs `SiriusLooper.app` BEFORE the POST_BUILD copies
   run, so the embedded helpers are not in the parent's
   CodeResources seal. Dev-loop ad-hoc signing works; Gatekeeper /
   notarization will reject the bundle without a re-sign step. Full
   diff is in `docs/operator/macos-sandbox.md`. Out of scope for
   M7 — the operator-pending CI signing session lands it.
3. **`sirius_plugin_host` is not a MACOSX_BUNDLE.** CMake's auto-ad-
   hoc-sign only fires for bundle targets. Same docs file tracks the
   fix.
4. **Test #272 ("permanent bypass: kill every generation") is
   timing-flaky under load.** S4 supervisor test, sensitive to
   process scheduling. Passes in isolation; flaked once under full-
   suite contention during step 3 verification and passed on retry.
   Not S6-introduced; pre-existing.
5. **Send-right ownership comment in `bootstrapXpcBridge` was
   wrong (now fixed).** `xpc_dictionary_copy_mach_send` returns an
   independently-owned send-right, NOT one whose lifetime is tied to
   the XPC connection. The intentional connection-leak is now
   documented as belt-and-suspenders that also keeps the
   reply-block's `claimMutex` reachable for the late-arrival
   protection path.
6. **`createPlaceholderView` helper removed from
   `OutOfProcessEditorView.mm`.** Logic inlined; then the review
   surfaced duplication and a small `applyPlaceholderTint` helper
   was re-extracted. The final shape is one helper, two callers.
7. **`#else !__APPLE__` SKIP test in
   `tests/CARemoteLayerRoundTripTests.mm` removed.** CMake gates the
   .mm on `if(APPLE)`, so the non-Apple SKIP test was unreachable
   dead code per the project's no-dead-code rule.

### First moves for the M7 S7 chat

S6 closes the V7 plan M7 line 487-554 acceptance criteria around GUI
embedding. M7 has remaining work that could be S7:

1. **Production-wire `MainComponent`** to consume
   `OutOfProcessEffectChainHost` + `OutOfProcessEditorView` —
   currently still deferred (continue.md decision #9 / S5 deviation
   list). Operator-facing "Add plug-in" UI. Logical next step before
   the M8 wet-capture work picks up `OutOfProcessPluginInstance`.
2. **Audio-ring SPSC violation fix** (carryover from S5
   deviation #1). Engine's `tryWriteBytes` (audio thread) +
   `sendBytes` (message thread) share the producer side of the
   engine→host ring. Latent today, becomes a real bug when an
   operator opens an editor mid-playback.
3. **Bundle re-sign for distribution** (S6 deviation #2 +
   `docs/operator/macos-sandbox.md`). Requires the operator-pending
   CI signing handoff.
4. **Windows + Linux GUI embedding** — per platform-order rule
   (`feedback_mac_first_linux_windows_last`), these are independent
   later sessions.

Suggested order: **MainComponent wiring first** (unblocks operator
eyes-on of the synthetic plug-in editor and any real CLAP). Then the
SPSC split. Re-sign + Linux/Windows wait on independent dependencies.

### S6-era decisions locked (S7 must preserve — superset of the S5 list)

(All S5-era items carry forward unchanged. New items:)

16. **Bundled XPC service brokers the Mach port.** Not anonymous
    Mach-port pair, not engine-as-MachService. The .xpc bundle ships
    inside `Contents/XPCServices/`; launchd brings it up on demand
    via the bundle's `CFBundleIdentifier`.
17. **Bridge protocol is `set_server_port` + `get_server_port`
    only.** Stateless broker; mach send-rights are reference-counted
    by the kernel. Unknown op replies with `{"error": "unknown_op"}`;
    missing op replies with `{"error": "missing_op"}` — "fail loud"
    per CLAUDE.md.
18. **Soft-fallback on missing bridge.** Children that can't reach
    the bridge fall back to the S5 placeholder layer; supervisor-
    restart logic unchanged. Engine-side
    `+[CALayer layerWithRemoteClientId:]` returns nil for unknown
    clientIds, falls back to the tinted placeholder.
19. **`IGuiBridge` port pattern** — third instance of the S3/S4
    dependency-inversion convention. Any future engine→host surface
    follows the same shape: pure-virtual port in `core/` (or
    `host/include/`), concrete impl in `host/src/`, test seam via
    `setInstanceForTesting` / `resetForTesting`.
20. **XPC bootstrap is `--mode clap` only.** Identity-mode tests
    don't pay the 250 ms timeout. The bootstrap lives in
    `host_process/main.cpp` and is `#ifdef __APPLE__`-gated.
21. **`-fno-objc-arc` on `OutOfProcessEditorView.mm` AND
    `gui_cocoa.mm`.** Both .mm files compile MRC-style; manual
    `release`, `invalidate`, `CGColorRelease`. The .mm in
    `PluginGuiBridge.mm` is ARC default (no manual release).

### Carryover NOT resolved (S7 doesn't touch unless flagged)

- **Bundle re-sign for distribution + `sirius_plugin_host`
  MACOSX_BUNDLE conversion** — `docs/operator/macos-sandbox.md`.
  Operator-pending CI signing session.
- **MainComponent has no production wiring** of
  `OutOfProcessEffectChainHost` or `OutOfProcessEditorView`. Most
  likely S7 candidate.
- **Pre-existing audio-ring SPSC violation** (engine
  `tryWriteBytes` + `sendBytes` share the producer side).
  Track as a separate follow-on.
- **Test #272 timing flake** — pre-existing S4 supervisor test;
  passes in isolation; rerun on flake.
- **`PluginIpcMessage::monotonicNs` LMC reinterpret** still pending
  the audio-thread LMC handle.
- **Carryover from M6 + earlier still unresolved** (ProcessedRoute
  empty span, manual operator-launch eyes-on of M6 surface, CI
  signing handoff) — unchanged from S5/S6-era state.

---

## HISTORICAL — M7 S5 close + M7 S6 handoff (superseded 2026-05-18 — M7 S6 now shipped)

## RESUME HERE (2026-05-18 — M7 S5 on origin/master; M7 S6 next)

**M7 S5 is on `origin/master`.** S5 head is `7e5caff` (the SHA-fill
doc commit); the feature commit is `52fcd7f`:

| SHA       | Subject |
|---|---|
| `52fcd7f` | M7 S5 — macOS GUI embedding (PluginGuiState + clap_gui_cocoa lifecycle, CARemoteLayer Mach handoff deferred to S6) |
| `7e5caff` | docs: continue.md — fill M7 S5 SHA (52fcd7f) |

Test count: **378/378** green (was 375 at S4; +3 in S5 — three
`[plugin-editor]` integration cases: Show round-trip, Hide release,
supervisor-restart re-publication).

**S5 made the editor lifecycle survivable end-to-end.** The complete
publish/poll IPC + JUCE component wrapper + supervisor restart re-show
all ship; the only piece deferred to S6 is the actual cross-process
GPU compositing (Apple's public path needs a Mach-port handoff that
isn't trivial without launchd/XPC). Concretely:

- **`PluginGuiState` shared region** (new `core/include/sirius/
  PluginGuiState.h`). Per-instance shm region with atomic seq-based
  request/response protocol: engine writes Kind/Width/Height then bumps
  `requestSeq` (release); host services and bumps `responseSeq` with
  `responseContextId` + size. Naturally MPMC-safe — sidesteps the
  pre-existing SPSC-violation of audio + message thread sharing the
  audio rings as producers. shm name `/sirius.<id>.gui` (30 chars,
  fits the macOS cap).
- **`clap_gui_cocoa` on the synthetic plug-in** (new
  `tests/fixtures/SyntheticTestPluginGui.mm`). Minimal 200×100 pt
  layer-backed NSView with a solid colour; `is_api_supported`,
  `create`, `set_parent`, `get_size`, `set_size`, `show`, `hide`,
  `destroy`. ARC off in this TU; manual retain/release brackets
  bracketed by the CLAP `create`/`destroy` callbacks.
- **GUI servicing in `host_process/main.cpp`**. The CLAP pump now
  attaches the GUI region as a third shm at startup, hands a
  `PluginGuiState*` to `runClapMode`, and services GUI requests both
  per-audio-iteration AND in `RingByteStream::readExact`'s sleep gap
  via an `OnIdle` callback. Bounds GUI latency to one audio buffer
  (~few ms) when busy and ~50µs when idle. Cocoa specifics live in
  the new `host_process/gui_cocoa.mm` (`-fno-objc-arc`) exposing
  `sirius_gui_show / hide / resize` C-linkage shims. The child binary
  stays JUCE-free; only adds `-framework AppKit -framework QuartzCore`
  on Apple.
- **Message-thread editor API on `OutOfProcessPluginInstance`**:
  `requestEditorShow / Hide / Resize`, `editorCaContextId`,
  `editorSize`, `editorRequestSeq`, `editorResponseSeq`. All are
  non-blocking publishers onto the GUI region — caller polls
  `editorCaContextId` to detect completion. Both ctors (identity +
  CLAP) create the GUI region; shutdown tears it down after the
  child reaps.
- **Slot-keyed forwarding on `OutOfProcessEffectChainHost`**:
  `requestEditorShow / Hide / Resize`, `editorCaContextId`,
  `editorSize`. Take `instancesMutex_` (message-thread only); return
  false/0 if slot is unconfigured or permanently bypassed.
  `SlotState` gains `editorWasShowing` + `editorWidth` +
  `editorHeight` so the supervisor's `attemptRestart` can re-issue
  Show against the freshly-spawned child BEFORE lowering the bypass
  fence. Restart re-publication is the third integration test.
- **`OutOfProcessEditorView`** — new `host/include/sirius/
  OutOfProcessEditorView.h` + `.cpp` + `.mm`. JUCE Component that
  wraps a placeholder NSView inside a `juce::NSViewComponent`. Polls
  the slot's CAContextID at 30 Hz; rebuilds the embedded view on
  change (initial publish OR supervisor-restart re-publish). Lazy:
  Show is issued on first paint when on-screen + sized.
  `MainComponent` NOT wired (per S5 acceptance criteria; the
  plug-in-adding UI session M20+ does production wiring).

**Measured RT (Apple Silicon, 2026-05-18):** `[plugin-ipc][.rt-smoke]`
holds at median 71 µs, p99 139 µs, max 141 µs. Well inside the 300 µs
p99 ceiling. GUI work is message-thread only; no audio-thread surface
added.

**Scope deviations locked in S5** (carry forward to S6):

1. **Atomic-seq shared region, not a third ring pair.** Reading the
   existing M7 surface revealed the engine→host audio ring already
   has two producers (audio-thread `tryWriteBytes` + message-thread
   `sendBytes`) — a latent SPSC violation harmless today because the
   two paths don't race in tests but would become a real bug as soon
   as the operator opens an editor mid-playback. Adding GUI messages
   to that ring would compound it. The professional fix is the
   dedicated `PluginGuiState` region (atomics, MPMC-safe). The
   pre-existing audio-ring SPSC violation IS NOT FIXED in S5 — it
   needs its own session (split sendBytes onto a dedicated message-
   thread ring, OR retire sendBytes once the audio path is the only
   real producer). Track as the "audio-ring SPSC split" follow-on.
2. **CARemoteLayer Mach-port handoff DEFERRED to S6.** Apple's public
   cross-process layer-publish API is `CARemoteLayerServer` (engine)
   + `CARemoteLayerClient` (child), which requires a Mach-port
   transfer. The legacy `NSMachBootstrapServer` path was removed in
   macOS 10.10; the modern path is XPC, which carries enough setup
   overhead (XPC service manifest, endpoint registration, connection
   lifecycle) to deserve its own session. **S5 ships the complete
   IPC + lifecycle + supervisor-restart re-publication contracts**;
   `gui_cocoa.mm` produces a process-unique non-zero placeholder
   contextId so the integration tests prove the round-trip works
   end-to-end. `OutOfProcessEditorView.mm` creates a coloured
   placeholder NSView so the operator-launch eyes-on path still
   shows SOMETHING (tint varies with contextId — a successful
   supervisor restart re-publish is visually obvious). When S6 lands
   the XPC + CARemoteLayer wiring, NO engine API changes — the
   contextId just starts pointing at a real remote layer.
3. **`CALayerHost` forward-decl pattern abandoned.** The earlier
   draft forward-declared CALayerHost (Chromium pattern) but it's
   SPI; the public path is CARemoteLayer*. S6 will use the public
   API.
4. **Editor lifecycle is lazy.** First on-screen + sized
   `parentHierarchyChanged` / `visibilityChanged` issues Show; nothing
   happens at host spawn or `configureBus`. Matches the V7 plan +
   continue.md S5 lean.
5. **macOS-only.** Windows + Linux GUI embedding land in their own
   later sessions per the
   `feedback_mac_first_linux_windows_last` rule. The .mm files +
   `-framework AppKit -framework QuartzCore` linkage are gated on
   `APPLE`.

### First moves for the M7 S6 chat

M7 S6's job is to land **real cross-process GPU compositing** via
`CARemoteLayerServer` / `CARemoteLayerClient` + XPC-based Mach-port
transfer. After S6, the placeholder contextId from
`host_process/gui_cocoa.mm` becomes a real `CARemoteLayerClient.
clientId`, and `OutOfProcessEditorView.mm`'s placeholder NSView
becomes `+[CALayer layerWithRemoteClientId:]`.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and
   re-read **M7 lines 487-554**, focusing on the sandbox /
   entitlement notes around line 550.
3. Read the XCSDK headers
   `/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.
   platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/
   QuartzCore.framework/Headers/CARemoteLayerClient.h` +
   `CARemoteLayerServer.h`. These define the public API.
4. **Brainstorm before code.** S6 open questions:
   - **XPC vs anonymous-Mach-port handshake**. Two options for
     transferring the `CARemoteLayerServer.sharedServer.serverPort`
     from engine to child:
     - (a) XPC service per-instance (manifest under
       `Contents/XPCServices/`, `xpc_connection_create_mach_service`
       on engine, `xpc_main` in a tiny XPC binary, child
       `xpc_connection_create` against the name). Cleaner. Plays
       nicely with sandbox + future MAS submission. Adds a third
       binary to the bundle.
     - (b) Per-instance Mach-port pair created via `mach_port_
       allocate` + sent over an anonymous UNIX-domain socket with
       `SCM_RIGHTS`-equivalent (sendmsg with `mach_msg_type_
       MACH_MSG_PORT_DESCRIPTOR`). No XPC binary. Less standard.
     **Lean (a) XPC** — more idiomatic, survives the eventual
     sandbox tightening.
   - **XPC connection per instance vs shared XPC service**. One
     shared XPC service that all child instances connect to is
     simpler to set up but adds a serialization point. Per-instance
     XPC connection is cleaner but adds a manifest entry per child
     spawn. **Lean per-instance** — matches the existing per-
     instance shm pattern, gives the supervisor a clean teardown
     hook on restart.
   - **Where does the XPC manifest live?** Currently the app bundle
     has `MacOS/Sirius Looper` and the existing `sirius_plugin_host`
     binary. Adding `XPCServices/com.sirius.gui-bridge.xpc/Contents/
     MacOS/sirius_gui_bridge` is the conventional layout.
5. **Brainstorm protocol**: same orchestrator + Backend Architect
   subagents. Code Reviewer before commit. The S6 work is delicate
   (Mach port lifetimes are unforgiving — the engine MUST release
   the server-side port at the exact right time or the child's
   layer-host gets a stale clientId that draws garbage).

### S6 acceptance criteria

- `CARemoteLayerServer.sharedServer.serverPort` accessible from the
  engine; transferred to the host child via the chosen XPC path.
- Host child's `gui_cocoa.mm` swaps the placeholder counter for a
  real `CARemoteLayerClient` whose `clientId` becomes the
  `responseContextId` written into `PluginGuiState`.
- Engine's `OutOfProcessEditorView.mm` swaps the placeholder NSView
  for `+[CALayer layerWithRemoteClientId:]` wrapped inside an NSView.
- Existing 378 ctest cases still pass; new tests for the XPC
  bootstrap (`[plugin-editor-xpc]` tag). Test count target ~381.
- Operator-launch eyes-on: actually see the synthetic plug-in's
  coloured NSView rendered inside the engine's window via
  CARemoteLayer compositing.

### S5-era decisions locked (S6 must preserve — superset of the S4 list)

1. **POSIX-only scope** (macOS first; Linux + Windows GUI in later
   sessions per platform-order rule).
2. **Host binary stays JUCE-free.** S5 added Cocoa direct linkage
   on macOS; S6 may add XPC linkage. NEVER add JUCE to host_process.
3. **Dependency-inversion port pattern** (S3 `IEffectChainHost`, S4
   `INotificationSink`). S5 didn't need a new port — editor work is
   host-side only. If S6 surfaces a third engine→host port (e.g. an
   XPC connection holder), follow the same pattern.
4. **`OutOfProcessPluginInstance` API split**:
   - Audio-thread (byte-oriented): `tryWriteBytes` / `tryReadBytes`
   - Message-thread (byte-oriented): `sendBytes` / `readBytes`
   - Message-thread (editor): `requestEditorShow` / `Hide` /
     `Resize` + `editorCaContextId` + `editorSize` (NEW in S5)
5. **Zero allocation / zero locks on the audio thread.** Unchanged.
6. **`NotificationBus` / `INotificationSink`** is the engine→UI
   truthfulness channel. S5 didn't add new posts (the existing
   supervisor restart Info / Error posts already cover the editor
   re-show case implicitly).
7. **Drop-NEW overflow policy** for SPSC rings. Unchanged.
8. **macOS shm_open name cap = 18 chars** for instance ids. S5's
   new `/sirius.<id>.gui` suffix (4 chars) fits.
9. **`IEffectChainHost` is the audio-thread port.** Unchanged.
10. **`PluginGuiState` is the editor publish/poll channel.** NEW in
    S5. Atomic seq-based; MPMC-safe. S6 keeps it unchanged — only
    the `responseContextId` value's *meaning* changes from
    placeholder to real CARemoteLayerClient.clientId.
11. **Pipelined 1-buffer delay** for audio-thread sync. Unchanged.
12. **Bypass-flag fence + 100ms grace** for restart protocol.
    Unchanged.
13. **`shared_ptr<SlotState>` ownership.** Unchanged.
14. **`kConsecutiveMissThreshold = 16`, `kMaxRestartAttempts = 3`,
    `kSupervisorPollMs = 50`, `kRestartGraceMs = 100`,
    `kPollMs = 33`** (S5 new — `OutOfProcessEditorView`'s timer
    cadence, 30 Hz GUI polling). Unchanged.
15. **Lazy editor lifecycle.** First on-screen Show; no editor at
    host spawn. Unchanged.

### Carryover NOT resolved (S6 doesn't touch unless flagged)

- **Pre-existing audio-ring SPSC violation** (engine `tryWriteBytes`
  + `sendBytes` share the producer side). Track as a separate
  follow-on; S5 made it observable but does not fix it.
- **MainComponent has no production wiring** of
  `OutOfProcessEffectChainHost` or `OutOfProcessEditorView`. The
  plug-in-adding UI session (M20+) wires both.
- **`PluginIpcMessage::monotonicNs` LMC reinterpret** still pending
  the audio-thread LMC handle.
- **Carryover from M6 + earlier still unresolved** (ProcessedRoute
  empty span, manual operator-launch eyes-on of M6 surface, CI
  signing handoff) — unchanged.

---

## HISTORICAL — M7 S4 close + M7 S5 handoff (superseded 2026-05-18 — M7 S5 now shipped)

## RESUME HERE (2026-05-18 — M7 S4 committed locally; M7 S5 next)

**M7 S4 is committed locally at `ae00237`** (push pending — see
end of this section). Single commit:

| SHA | Subject |
|---|---|
| `ae00237` | M7 S4 — plug-in watchdog + supervisor + PluginEvent posts (shared_ptr SlotState, bypass-flag fence) |

Test count: **375/375** green (was 368 at S3; +7 in S4 — 4
`[plugin-supervisor][unit]` slot-state cases + 3
`[plugin-supervisor]` integration cases driving real host
SIGKILL + restart cycles).

**S4 made the audio call chain survivable.** RT_SAFETY_CONTRACT
§5's promise ("no plug-in failure mode can produce an audio-thread
glitch") is now load-bearing. Concretely:

- **Per-slot watchdog** lives inside `pumpSlot` — per-slot
  `std::atomic<std::uint32_t> consecutiveMisses` increments on
  `tryWriteBytes` ring-full OR `tryReadBytes` empty-after-write,
  resets on full success. Supervisor reads with
  `memory_order_relaxed` at 50ms polling cadence.
- **Off-thread supervisor** is a single `std::thread` member on
  `OutOfProcessEffectChainHost`. Started in the ctor, joined in
  the dtor BEFORE `instances_` destruction. Wakes on
  `condition_variable` + 50ms tick (TapeWriter pattern).
- **Bypass-flag fence restart protocol**: supervisor sets
  `bypassed_=true` (release) → sleeps `kRestartGraceMs = 100` ms
  (audio buffer ≪ 100ms; pumpSlot itself <1ms; so any in-flight
  pumpSlot completes inside this window) → `state.instance.reset()`
  → `state.instance = std::make_unique<OutOfProcessPluginInstance>(...)`
  inside try/catch → resets miss counter → `bypassed_.store(false)`
  (release). Posts `Info / PluginEvent / "<instanceId>
  restarted (attempt N of 3)"` to NotificationBus.
- **Permanent bypass**: after `kMaxRestartAttempts = 3` restart
  attempts, slot is permanently bypassed via
  `permanentlyBypassed_=true` (audio thread dry-on-misses
  forever). Posts `Error / PluginEvent` to NotificationBus.
  Slot's instance is destroyed; no further attempts.
- **Spawn-failure handling**: if the new
  `OutOfProcessPluginInstance` ctor throws (shm exhaustion,
  missing binary, etc.), the supervisor catches, posts
  `Warning / PluginEvent` (stack `char[128]` + `snprintf` — no
  heap allocation on the supervisor thread), leaves
  `bypassed_=true` and `instance` empty, increments
  `restartCount`. Next supervisor cycle escalates to permanent
  bypass if `restartCount >= kMaxRestartAttempts`.
- **Map ownership**: `instances_` is
  `std::unordered_map<SlotKey, std::shared_ptr<SlotState>>`.
  The supervisor snapshots `shared_ptr` copies into a local
  vector before releasing `instancesMutex_`, so a concurrent
  `configureBus` erase cannot free a SlotState mid-restart. The
  audio thread continues to access via `it->second.get()` — NO
  shared_ptr refcount manipulation on the audio path; the
  refcount is only touched by message thread (configureBus
  emplace/erase) and supervisor thread (snapshot copy + drop).

**Measured RT cost (Apple Silicon dev machine, 2026-05-18):** the
S2c-era `[plugin-ipc][.rt-smoke]` regression check passes with
median 67 µs, p99 139 µs, max 143 µs — well inside the 300 µs p99
ceiling. S4's audio-path additions (2 atomic acquire-loads + 1
atomic fetch_add on the miss path; 1 atomic store on the success
path) are invisible at this resolution. The shared_ptr ownership
refactor is invisible too because the audio path is unchanged in
shape (raw pointer deref via `it->second.get()`).

**Scope deviations locked in S4** (carry forward):

1. **`INotificationSink` port + enum move to `core/`.** The
   original plan said host depends on engine for NotificationBus;
   reality is `host/CMakeLists.txt` only links `Sirius::Core`. The
   professional fix (locked in S3 with `IEffectChainHost`): create
   a pure-virtual port in `core/` and have the concrete engine
   class implement it. `core/include/sirius/INotificationSink.h`
   is the new port; `NotificationLevel` + `Category` enums moved
   to it. `NotificationBus` in `engine/` now `: public
   INotificationSink`. All existing `sirius::NotificationLevel::X`
   / `sirius::Category::Y` spellings work unchanged across
   InputMixer / AudioCallback / TapeWriter / the 3 NotificationBus
   test files (no call-site edits). **This is now the canonical
   pattern** for any future engine surface host needs to talk to.
2. **Supervisor starts in the ctor**, not lazily on first
   `configureBus`. Symmetric lifecycle (ctor starts, dtor joins),
   no first-configure race against the audio thread.
3. **SlotState doesn't store `busId`/`slotIndex`.** The map key
   already carries them; only `makeRestartInstanceId` needs them.
4. **Test-only `childPidForTestingAtSlot(busId, slotIndex)`**
   added to `OutOfProcessEffectChainHost`. The permanent-bypass
   test needs to SIGKILL each successive child generation; the
   accessor lets it reach the new pid after each restart.
5. **Permanent-bypass test polls for forward progress** (gate
   each restart iteration on `restartCountForTesting` advancing,
   3-second per-iteration timeout) instead of fixed sleeps.
   Deterministic under any CI load; flakes only on genuine
   supervisor failure.
6. **`shared_ptr<SlotState>` ownership** (B1 reviewer-found
   blocker fix). Closes a use-after-free hole where a concurrent
   `configureBus` erase could free a SlotState mid-restart while
   the supervisor was inside the 100ms grace + spawn window. The
   set-once contract makes this theoretical today, but the
   MainComponent plug-in-adding UI (M20+) will reconfigure during
   audio and would have hit this. Fixed now while the surface is
   small.
7. **Off-by-one fix landed**: comparison is `restartCount >=
   kMaxRestartAttempts` (was `>`). With `kMaxRestartAttempts =
   3`, three restart attempts total; the 4th supervisor cycle
   posts permanent bypass.
8. **LMC handle to pumpSlot — STILL not wired in S4.** Continues
   to defer (per M7 decision #9). `PluginIpcMessage::monotonicNs`
   stays 0 in the audio-thread path. S5+ wires it when the
   M7 decision-9 reinterpret becomes load-bearing.
9. **`MainComponent` STILL not wired.** Same as S3. The
   plug-in-adding UI session (M20+) does the production wiring.

### First moves for the M7 S5 chat

M7 S5 adds **macOS GUI window embedding**. The plug-in's editor
window opens in the separate `sirius_plugin_host` process, but
the operator-facing window must visually live INSIDE the Sirius
Looper process's UI. The mechanism is **NSView reparenting via
`clap_gui_cocoa`**: the child process creates the editor NSView,
hands the parent its handle, and the parent reparents that view
into its own NSView hierarchy.

This is the platform-specific session per V7 plan M7 lines
487-554. macOS only this session (per the
`feedback_mac_first_linux_windows_last` platform-order rule);
Windows + Linux variants come in their own much-later sessions.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and
   re-read **M7 lines 487-554**. GUI-embedding work is in the
   acceptance criteria (lines 491-501) and listed as Session
   4-N (estimated) at line 535. S5 takes the macOS slice.
3. Read V7 §9.1 in the white paper for the embedding spec.
4. Read JUCE's `juce::AudioProcessorEditor` + `juce::Component`
   docs for the parent-window side. The out-of-process twist is
   the cross-process reparenting.
5. **Brainstorm before code.** S5 open questions:
   - **CGSConnection vs NSView pointer over Mach IPC.** macOS
     has two APIs for cross-process view sharing. CGS is
     simpler; NSView/Mach is more sandbox-friendly. V7 plan
     line 552 mentions "macOS sandbox entitlements" —
     reconcile with the CI signing handoff (still operator-
     pending) before picking. **Lean toward CGS for S5**
     (simpler, no entitlement work yet); sandbox-aware NSView
     migration deferred to whenever the CI signing surface
     forces it.
   - **CLAP GUI extension**. The CLAP spec has `clap_gui` with
     platform-specific entry points (`clap_gui_cocoa` for
     macOS). For macOS, the plug-in gives the host an NSView
     handle. `sirius_plugin_host` child binary needs to add
     `clap_gui_cocoa` interaction in its CLAP pump loop.
   - **SyntheticTestPlugin** needs a minimal `clap_gui_cocoa`
     impl so the S5 integration test has something to embed.
     Tiny addition — a single NSView with a fixed background
     color is enough.
   - **IPC message kind**. Add `PluginIpcMessage::Kind::GuiShow`
     and `GuiHide` (and possibly `GuiResize`). Out-of-band
     from the audio-thread message kind; uses the same SPSC
     ring but at message-thread cadence so no RT impact.
   - **Editor lifecycle**. When does the child create the
     editor? Lazily on first `gui-show`, or eagerly at host
     spawn? Lazy is leaner (saves resources if operator never
     opens the editor); eager is simpler. **Lean lazy**.
6. **Brainstorm protocol**: design the embedding API first.
   - `OutOfProcessPluginInstance` gains
     `bool requestEditorShow() noexcept` (message-thread) +
     `bool requestEditorHide() noexcept` + accessor for the
     returned NSView handle once ready.
   - The CLAP-mode pump loop in `host_process/main.cpp` adds
     a "gui" message branch: parent sends `gui-show`, child
     dlopens `clap_gui_cocoa`, creates the editor view,
     replies with the view handle.
   - The parent UI (eventually `MainComponent`, but S5 stays
     test-driven) creates a `juce::Component` wrapper
     (`OutOfProcessEditorView` in `host/`) that embeds the
     NSView at the right Z-order via JUCE's `setColour` +
     `addAndMakeVisible` pattern adapted for foreign views.
7. Adopt the same orchestrator+subagents execution mode.
   Backend Architect for the cross-process view-sharing
   wiring (NSView Mach port serialization is delicate);
   Code Reviewer before commit (found 2 real blockers in
   S4 — keep the pattern).

### S5 acceptance criteria (V7 plan lines 491-501 + first-moves above)

- `sirius_plugin_host` child binary opens the CLAP plug-in's
  editor via `clap_gui_cocoa` when the parent sends a
  `gui-show` IPC message. Returns the NSView handle via the
  reply message.
- `OutOfProcessPluginInstance` exposes message-thread
  `requestEditorShow()` + `requestEditorHide()` + accessor
  for the returned NSView handle.
- A JUCE `Component` wrapper (probably `OutOfProcessEditorView`
  in `host/`) embeds the NSView into the parent process's
  NSView hierarchy at the specified bounds.
- Integration test loads the synthetic CLAP plug-in's editor
  (synthetic plug-in needs a minimal `clap_gui_cocoa` impl —
  add to `tests/fixtures/SyntheticTestPlugin`), embeds it
  into a dummy JUCE window, verifies the NSView is parented
  (e.g. by checking the view hierarchy contains the
  cross-process view handle). Headless GUI test — acceptable
  to skip under CI if JUCE's offscreen window doesn't
  support the embedding, but should pass when driven by an
  operator from a real `.app`.
- All existing 375 ctest cases still pass; new tests for the
  GUI embedding (`[plugin-editor]` tag). Test count target:
  ~377-380.
- `RT_SAFETY_CONTRACT.md §6` does NOT change (GUI is
  message-thread only; no audio-thread surface).
- `MainComponent` STILL not wired (the plug-in-adding UI is
  a separate later session).

### M7-era decisions locked (S5 must preserve — superset of the S4 list)

1. **POSIX-only scope for non-GUI work** (macOS + Linux). GUI
   embedding is macOS-only this session.
2. **Host binary stays JUCE-free.** S5 adds NSView creation in
   the child binary via direct Cocoa (no JUCE) — `Sirius::Core`
   stays JUCE-free; the host binary may grow Cocoa direct
   linkage on macOS only.
3. **Dependency-inversion port pattern** is load-bearing
   convention (S3 `IEffectChainHost`, S4 `INotificationSink`).
   If S5 needs a third engine→host surface, define a port in
   `core/` and have the engine class implement it.
4. **OutOfProcessPluginInstance public API split**: byte-
   oriented audio-thread (`tryWriteBytes`/`tryReadBytes`) +
   byte-oriented message-thread (`sendBytes`/`readBytes`) +
   editor message-thread (S5 adds `requestEditorShow` /
   `requestEditorHide`). GUI methods are NOT audio-thread
   surface.
5. **`isRunning()` non-const + silently reaps**. Unchanged.
6. **Zero allocation / zero locks on the audio thread.**
   Unchanged.
7. **`NotificationBus` / `INotificationSink`** is the engine→UI
   truthfulness channel. S5 may post editor-failure events
   (e.g. "<instanceId> editor failed to open") at
   `Warning / PluginEvent`.
8. **Drop-NEW overflow policy** for SPSC rings. Unchanged.
9. **macOS shm_open name cap = 18 chars** for instance ids
   (`kMaxPluginInstanceIdLength`). Unchanged.
10. **`IEffectChainHost` is the audio-thread port; concrete
    `OutOfProcessEffectChainHost` is the only impl.** Tests may
    use a stub. S5 does not change this.
11. **Pipelined 1-buffer delay** for audio-thread sync.
    Unchanged. S5 is GUI-only.
12. **Bypass-flag fence + 100ms grace** for restart protocol.
    Unchanged from S4.
13. **`shared_ptr<SlotState>` ownership** in the slot map.
    Unchanged from S4.
14. **`kConsecutiveMissThreshold = 16`,
    `kMaxRestartAttempts = 3`, `kSupervisorPollMs = 50`,
    `kRestartGraceMs = 100`**. Unchanged from S4.

### Carryover NOT resolved (S5 doesn't touch unless flagged)

- **Push of S4 to `origin/master` is pending operator
  go-ahead.** S4 is committed locally at `ae00237`. Per
  standing rule (memory:
  `feedback_claude_commits_and_pushes_master`) Claude is
  authorized to push. If operator says go: `git push origin
  master` (no force, no PR).
- **MainComponent has no production wiring** of
  `OutOfProcessEffectChainHost` or its editor surface.
  Plug-in-adding UI session (M20+) wires both.
- **`PluginIpcMessage::monotonicNs` LMC reinterpret** still
  pending the audio-thread LMC handle.
- **Carryover from M6 + earlier still unresolved**
  (ProcessedRoute empty span, manual operator-launch eyes-on
  of M6 surface, CI signing handoff) — all unchanged from
  S3/S4-era state. None block M7.

---

## HISTORICAL — M7 S3 close + M7 S4 handoff (superseded 2026-05-18 — M7 S4 now shipped)

**M7 S3 is committed locally at `566d1a0`** (push pending operator
go-ahead — the operator may want eyes-on-diff before it lands on
origin/master). Single commit:

| SHA | Subject |
|---|---|
| `566d1a0` | M7 S3 — wire OutOfProcessPluginInstance through Bus::process effect chain (pipelined, IEffectChainHost dependency-inverted) |

Test count: **368/368** green (was 364 at S2; +4 in S3 — 3
`[plugin-ipc][audio-thread]` cases + 1 `[output-mixer][plugin-host]`
integration case with 1040 sample assertions through the real
synthetic CLAP).

**S3 wired the IPC layer onto the audio call chain for the first
time.** Concretely:
- `IEffectChainHost` (pure-virtual, `core/`, JUCE-free) defines the
  port `Bus::process` calls per non-bypassed slot.
- `OutOfProcessEffectChainHost` (`host/`, JUCE-bearing) owns
  `unordered_map<(BusId.value(), slotIdx) → unique_ptr<OutOfProcessPluginInstance>>`,
  spawns instances on `configureBus(...)` (message-thread), and
  implements `pumpSlot(...)` (audio-thread, noexcept, wait-free).
- `OutOfProcessPluginInstance` grew audio-thread siblings
  `tryWriteBytes`/`tryReadBytes` (single SPSC push/pop, no spin,
  no timeout, noexcept). The existing message-thread
  `sendBytes`/`readBytes` are unchanged.
- `Bus::process` grew a chain-dispatch path: when the chain has
  any non-bypassed entry AND `host_ != nullptr`, copy `mixBuffer_
  → processedBuffer_`, call `host_->pumpSlot` per entry (in-place
  in `processedBuffer_`), then additively sum `processedBuffer_`
  into `output`. Empty-chain / all-bypassed / host-nullptr falls
  back to the M5 path bit-for-bit (zero perf regression).
- `OutputMixer::setEffectChainHost(...)` forwards to every existing
  bus AND stashes the host so `addBus`-after-set still wires
  correctly. Step 3 of `renderBuffer` takes the `Bus::process`
  path (writing into master's mixBuffer) for any aux bus with an
  active chain; empty-chain aux buses stay on the M5 inline
  accumulate-and-zero path.

**RT model: pipelined, 1-buffer delay.** Audio thread does
`tryWriteBytes(buf N)` then `tryReadBytes(response for buf N-1)`.
Never blocks. On empty pop → `processedBuffer_` keeps the dry
pre-pump copy → additive sum produces dry output (NOT silence /
NOT garbage). Steady-state output[k] == input[k-1] for k ≥ 1;
output[0] == dry. The integration test exercises this: 8 stereo
buffers (left = k+1, right = -(k+1)) through real synthetic CLAP,
asserts pipelined relationship sample-exact (1040/1040
assertions pass).

**Measured RT cost (Apple Silicon dev machine, 2026-05-18):** the
S2c-era `[plugin-ipc][.rt-smoke]` regression check still passes
with median 81 µs, p99 162 µs, max 165 µs — well inside the 300 µs
p99 ceiling. S3 added no measurable latency to the S2c baseline
(the SPSC primitives are the load-bearing path; the new audio-
thread wrappers around them are 3-line forwards).

**Scope deviations locked in S3** (carry forward):

1. **`IEffectChainHost::pumpSlot` takes `int64_t busId`, not the
   strong-typed `BusId`.** `BusId` lives in
   `engine/include/sirius/Channel.h`; promoting it to `core/` would
   be unrequested scope creep. The interface stays engine-
   independent; engine call sites pass `id_.value()`. Strong-typing
   lives at the call site, not the abstraction.
2. **Response wire format has NO `uint32_t frameCount` header.**
   The plan implied symmetric framing (input has header, response
   mirrors), but `host_process/main.cpp:438-444` writes raw float
   bytes via `writeAll`+`flush` — no header on the response side.
   `pumpSlot` reads response bytes as raw floats and computes
   `framesAvailable` from byte count. Matched the existing host
   wire format rather than changing the host child.
3. **`PluginIpcMessage::monotonicNs` is set to 0 in `tryWriteBytes`**
   (audio-thread path). The audio-thread caller doesn't yet have an
   LMC handle surfaced to it; S4+ watchdog work fills this in. The
   message-thread `sendBytes` still writes `steady_clock` ns so the
   existing `[plugin-ipc][.rt-smoke]` latency regression case keeps
   working unchanged. The header docblock reflects this dual-mode
   reality.
4. **Per-instance ID budget is 18 chars** (encoded as
   `kMaxPluginInstanceIdLength` in `core/PluginInstanceId.h`),
   derived from macOS shm_open's 31-char total cap minus the
   `/sirius.<id>.<suffix>` framing. `OutOfProcessEffectChainHost::
   makeInstanceId(busId, slotIdx)` builds compact `bN_sM` ids and
   hashes via FNV-1a only if the raw form would exceed the budget
   (defensive — typical small ids pass through).
5. **`tryWriteBytes` max payload ceiling is `kMaxPayloadBytes = 8192`
   bytes**, which with the 4-byte frameCount header means
   `frameCount ≤ 1023` for stereo (the V7 plan's "1024-frame outer
   envelope" is one frame over). Realistic block sizes (64..512)
   fit comfortably. Caller violation on oversize → false return,
   no crash. Documented in both files' inline comments.
6. **The integration test "65th-push rejects" deterministic case
   was dropped.** Catch2's `FatalConditionHandler` interacted badly
   with the in-process test sequencing on macOS (SIGTERM signals
   from the instance destructor surfaced as fatal). The SPSC
   full-ring → false contract is exhaustively covered by
   `SharedMemorySpscQueueTests.cpp`; `tryWriteBytes` is a 3-line
   wrapper, so the missing wrapper-level case is non-load-bearing.
   `OutOfProcessPluginInstanceAudioThreadTests.cpp` documents this
   explicitly.
7. **No `MainComponent` wiring in S3.** The host has no production
   consumer yet — only the integration test exercises it. When the
   plug-in-adding UI surfaces (M20+), `MainComponent` will own the
   `OutOfProcessEffectChainHost` and inject it into the OutputMixer
   per the M5/M6 set-once pattern. S3 leaves this for then.

### First moves for the M7 S4 chat

M7 S4 adds the **watchdog + supervisor + plug-in NotificationBus
events**. S3 wired the call chain; S4 makes it survivable. The
RT_SAFETY_CONTRACT §5 promise ("no plug-in failure mode can
produce an audio-thread glitch") becomes load-bearing in S4.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and
   re-read **M7 lines 487-554**. V7 §9.1 acceptance criteria
   (lines 491-501) specifically call out: watchdog bounding
   per-buffer time; supervisor observing misses + restarting +
   posting NotificationBus events; persistent misses → permanent
   bypass. S4 covers these.
3. Read `engine/include/sirius/NotificationBus.h` (M6) + the
   `PluginEvent` category. S4's supervisor posts into the existing
   bus — do NOT spin a parallel signal channel (continue.md M6
   decision #1 applies).
4. **Brainstorm before code.** S4 open questions:
   - **Where does the watchdog timer live?** Audio-thread cannot
     spawn a thread mid-buffer. Options:
     (a) Per-pumpSlot rdtsc/steady_clock measurement + threshold
         check inside `pumpSlot` itself (cheap, audio-thread, the
         deadline-miss surfaces as a `processedBuffer_` reset to
         dry within the same buffer).
     (b) Off-thread watcher thread that polls every instance's
         "last-write timestamp" atomic and signals on stall.
     Almost certainly (a) for the per-buffer deadline + (b) for
     the persistent-stall escalation. (a) is the watchdog;
     (b) is the supervisor.
   - **Persistent-miss → permanent bypass policy.** What N
     consecutive misses trigger bypass? V7 plan suggests
     transient → log; persistent → restart; repeated → bypass.
     Pick concrete N values + brainstorm with operator.
   - **Restart semantics.** Supervisor SIGKILLs the dead host,
     reconstructs `OutOfProcessPluginInstance`, swaps it into the
     `OutOfProcessEffectChainHost` map. The swap must be atomic
     from the audio thread's POV (atomic snapshot pointer, M6 §2
     pattern). The audio thread reads through the snapshot; the
     supervisor publishes a new one. Existing in-flight pumpSlot
     calls finish on the old instance; subsequent calls see the
     new one.
   - **PluginEvent category in NotificationBus.** M6 ships the
     category enum + ring infrastructure. Verify the
     `PluginEvent` category exists (it should — continue.md M6 #1
     mentions watchdog use). Confirm before writing post sites.
5. Adopt the same orchestrator+subagents execution mode. Backend
   Architect for the watchdog + supervisor wiring; Code Reviewer
   pass before commit (caught the off-by-one in S3 docs).

### S4 acceptance criteria (V7 plan lines 491-501 + continue.md M6 #1)

- Per-buffer watchdog inside `pumpSlot` (or in `Bus::process`
  around the pumpSlot call) measures elapsed time per slot.
  Threshold miss → slot bypassed for this buffer
  (`processedBuffer_` reset to mixBuffer copy → dry output for
  this slot, then continue to next slot).
- Off-thread supervisor watches per-instance "last successful
  pump" timestamps. Three consecutive missed buffers → restart
  via SIGKILL + reconstruct; five consecutive missed buffers
  after restart → permanent bypass + `NotificationBus::post(
  Error, PluginEvent, "<instanceId> permanently bypassed after
  repeated failures")`.
- Restart path uses atomic-snapshot publish for the instance
  pointer in `OutOfProcessEffectChainHost`'s slot map (M6 §2
  pattern). Audio-thread pumpSlot calls finishing on the old
  instance are correct; subsequent calls see the new one with
  no torn read.
- `pumpSlot` audio-thread surface in `docs/RT_SAFETY_CONTRACT.md
  §6` updated for the new watchdog measurement path.
- All existing 368 ctest cases still pass; new tests for the
  watchdog + supervisor cycle (`[plugin-watchdog]`,
  `[plugin-supervisor]` tags). Test count target: ~373-378.
- Operator-launch eyes-on of the new NotificationBus surface
  (the M6 surface gains plug-in events) optional but
  recommended before S4 commit.

### M7-era decisions locked (S4 must preserve — superset of the S3 list)

1. **POSIX-only scope** (macOS + Linux; Windows defers).
2. **Host binary stays JUCE-free.** CLAP + SHM primitives are in
   `Sirius::Core` (JUCE-free) for exactly this reason. **DO NOT**
   link the host binary against `Sirius::Engine` or `Sirius::Host`.
3. **`OutOfProcessPluginInstance` public API is byte-oriented** —
   `sendBytes`/`readBytes` (message-thread) and
   `tryWriteBytes`/`tryReadBytes` (audio-thread, S3). S4 adds the
   watchdog around the call site (in `Bus::process` or
   `pumpSlot`); does NOT add new bytes APIs.
4. **`isRunning()` stays non-const + silently reaps + clears
   `childPid_`.** S4 supervisor uses this to detect dead children.
5. **Zero allocation / zero locks on the audio-thread side.** The
   SPSC primitive is wait-free; SHM regions are created at
   construction time (message thread). S4 must NOT introduce any
   audio-thread allocation, including in the watchdog measurement
   path.
6. **`NotificationBus` (M6) is the engine→UI truthfulness
   channel.** S4 wires `PluginEvent` posts here; do NOT create a
   parallel channel.
7. **Drop-NEW overflow policy** for SPSC rings — `tryWriteBytes`
   returns false on full, `pumpSlot` returns false on either
   push-full or pop-empty. S4 watchdog interprets repeated
   push-full as a backpressure event (host not pulling fast
   enough) distinct from the deadline-miss case (host pulling
   but processing too slow).
8. **macOS shm_open name length cap (31 chars including leading
   slash).** S3 encoded this as `kMaxPluginInstanceIdLength = 18`
   in `core/PluginInstanceId.h`. S4's restart path mints new ids
   for the restarted instance (the supervisor uses the same
   helper); same budget applies.
9. **`PluginIpcMessage::monotonicNs`** is LMC-domain in
   `sendBytes` (S2c — actually steady_clock ns; the LMC reinterpret
   is documented but not yet load-bearing) and 0 in
   `tryWriteBytes` (S3). S4 watchdog fills the audio-thread side
   in when it surfaces an LMC handle to `pumpSlot`.
10. **`IEffectChainHost` is the audio-thread port.** Bus holds a
    raw `IEffectChainHost*`; the concrete
    `OutOfProcessEffectChainHost` is the only impl. S4 may add a
    test double (`StubEffectChainHost`) that implements the
    interface for unit-testing watchdog scenarios without
    spawning real host processes.
11. **Pipelined 1-buffer delay** is the audio-thread sync model.
    S4 watchdog does NOT change this — it triggers dry
    substitution on a miss, but the steady-state behaviour stays
    1-buffer pipelined. Plug-in delay compensation (PDC) is M8+.

### Carryover from S3 NOT resolved (M7 doesn't touch unless flagged)

- **Push to `origin/master` is pending operator go-ahead.** S3 is
  committed locally at `566d1a0`. Per the standing rule (memory:
  `feedback_claude_commits_and_pushes_master.md`) Claude is
  authorized to push, but the plan deferred per the M7 S1/S2
  cadence convention. Operator decides: push now, or eyes-on
  first. If push: `git push origin master` (no force, no PR).
- **MainComponent has no production wiring of
  `OutOfProcessEffectChainHost`.** Deferred to the plug-in-adding
  UI session (M20+). S4 does NOT need to add this.
- **`PluginIpcMessage::monotonicNs` LMC reinterpret is
  documented but not yet load-bearing.** S4 may surface the LMC
  handle to `pumpSlot` and fill the slot with a real sample
  index for the first time.
- **Carryover from M6 + earlier still unresolved** (ProcessedRoute
  empty span, manual operator-launch eyes-on of M6 surface,
  CI signing handoff) — all unchanged from S2-era state. None
  block M7.

---

## HISTORICAL — M7 S2 close + M7 S3 handoff (superseded 2026-05-18 — M7 S3 now shipped)

**M7 Sessions 1 + 2 are on `origin/master`.** S2 head is `8e95503`.
Four sub-session commits landed for S2:

| SHA | Subject |
|---|---|
| `de3179b` | M7 S2a — CLAP via setup-deps + SyntheticTestPlugin + host `--mode clap` |
| `c0cf0d6` | M7 S2b — SharedMemoryRegion (shm_open/mmap RAII) + SharedMemorySpscQueue<T> |
| `6e1df55` | M7 S2c — swap stdin/stdout for shared-mem SPSC rings + PluginIpcMessage |
| `8e95503` | M7 S2d — round-trip latency [.rt-smoke] + RT_SAFETY_CONTRACT §5 row |

Test count: **364/364** green (was 354 at S1; +1 CLAP smoke in S2a, +9 in
S2b for 6 SPSC tests + 3 region tests, hidden latency smoke in S2d).
The IPC transport is now POSIX shared-memory SPSC rings — `stdin`/`stdout`
piping is gone from both engine and host sides. The host child binary
**stays JUCE-free** (CLAP is header-only; shm primitives moved to JUCE-
free `Sirius::Core`).

**Measured round-trip latency (Apple Silicon dev machine, 2026-05-18):**
median 68 µs, p99 134-135 µs across 1000 samples. Hidden `[plugin-ipc]
[.rt-smoke]` asserts p99 < 300 µs (≈2× headroom). The V7 plan's < 10 µs
target is aspirational — it requires the lock-free spin transport the S4+
watchdog work will introduce; the S2c `kRingPollMicroseconds = 50` poll
backoff dominates the current baseline.

**Scope deviations locked in S2** (carry forward):

1. **CLAP arrives via `bash/setup-deps.sh`, NOT git submodule.** continue.md
   pre-S2 recommended submodule; that contradicted the actual project
   pattern (external/ is gitignored + populated by `setup-deps.sh`,
   matching OTTO's layout). S2a extended setup-deps.sh with
   `clap|free-audio/clap.git|1.2.6`. Operators on a fresh clone now run
   `bash bash/setup-deps.sh` to populate `external/clap`.
2. **SHM primitives live in `core/`, not `engine/`.** Initially placed in
   engine in S2b (where `LockFreeSpscQueue` lives), then moved to core in
   S2c when wiring revealed that linking the host binary against
   `Sirius::Engine` would pull `juce_core` (engine's PRIVATE dep) and
   violate the "host stays JUCE-free" constraint. `Sirius::Core` is the
   JUCE-free pure library, and the SHM primitives belong there.
   `PluginIpcMessage.h` also moved core-side so the host child can
   include it without depending on `SiriusHost` (which carries JUCE).
3. **`PluginIpcMessage::monotonicNs` is `int64_t` ns since
   `steady_clock` epoch, NOT an LMC `Rational`.** The plan said "LMC
   timestamps in headers" but LMC time is `Rational` (non-trivially-
   constructible), which can't live in a `static_assert(trivially_copyable)`
   POD. S3 will reinterpret the slot as LMC-domain when wiring the audio
   thread; for S2c-era round-trip measurement, monotonic ns is the
   meaningful unit.
4. **`OutOfProcessPluginInstance` gained a second constructor**
   `(hostBinaryPath, instanceId, clapPluginBundle)` for CLAP-mode spawn,
   additive to the S1 identity-mode constructor. Existing tests build
   binary-compatible.
5. **CLAP-mode `process()` uses a `RingByteStream` abstraction** inside
   `host_process/main.cpp` — reads the existing framed wire format
   (`uint32_t frameCount` + interleaved stereo float pairs) on top of
   message-by-message ring pops. Audio buffer ceiling is
   `PluginIpcMessage::kMaxPayloadBytes = 8192` = 1024 stereo float
   frames — matches the V7 outer block-size envelope.
6. **macOS shm pages are 16 KiB, not 4 KiB.** Apple Silicon kernel
   rounds shm segments up to a 16 KiB boundary; `SharedMemoryRegion::
   size()` re-stats unconditionally so both sides agree on the
   actual mapped bytes. Test assertions use `size() >= requested`.

### First moves for the M7 S3 chat

M7 S3 wires `OutOfProcessPluginInstance` into `OutputMixer`'s plug-in
slot on a Constituent's `EffectChain`. This is **the first session that
puts the IPC layer on the audio call chain** — design care required.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and re-read
   **M7 lines 487-554**. Session 3 is enumerated at line 533: "S3:
   Engine-side `OutOfProcessPluginInstance` wires through `OutputMixer`'s
   plug-in slot on a Constituent's `EffectChain`; integration test plays
   audio through the synthetic plug-in; commit."
3. Read `engine/include/sirius/Bus.h` + `engine/include/sirius/OutputMixer.h`
   to find the EffectChain entry point. M5 Session 3 noted: "When M7
   wires real plugin invocation through EffectChain, the inline path
   must become a real Bus::process invocation." S3 makes that real.
4. Read `core/include/sirius/EffectChain.h` (the M5-era stub that holds
   plug-in slots). S3 fills the slot with a real
   `OutOfProcessPluginInstance` invocation.
5. **Brainstorm before code.** S3 open questions:
   - **Audio-thread `sendBytes`/`readBytes`?** Current API is message-
     thread per the header docblock. The shm push/pop primitives are
     wait-free, so the API *can* be made audio-thread-safe — but the
     current `usleep` backoff in `readBytes` violates RT-safety. S3
     needs an audio-thread variant: zero-wait `tryReadBytes` returning
     immediately if the ring's empty. Watchdog (S4) handles the missed-
     deadline case.
   - **EffectChain → OutOfProcessPluginInstance shape.** EffectChain
     currently holds `PluginDescriptor` slots. S3 either: (a) extends
     EffectChain to own the OutOfProcessPluginInstance per slot, or
     (b) introduces a sibling `EffectChainHost` that owns the spawned
     processes and EffectChain stays descriptor-only. (b) keeps Core's
     JUCE-free contract (OutOfProcessPluginInstance is in SiriusHost
     which has JUCE deps); (a) would force Core to grow a JUCE dep.
     **Lean (b)**.
   - **Where instance ownership lives.** `MainComponent` owns mixers
     per the M5+M6 pattern; should it also own the `EffectChainHost`
     (or per-slot OutOfProcessPluginInstances)? Or does the OutputMixer
     own them via injection? The M5-era injection pattern says owner
     stays in MainComponent; OutputMixer holds raw pointers.
   - **Integration test shape.** S3 acceptance criterion: "plays audio
     through the synthetic plug-in." The synthetic plug-in is identity-
     only this session, so the test asserts byte-for-byte audio round-
     trip through an EffectChain slot. Catch2 tag suggestion:
     `[output-mixer][plugin-host]`.
6. Adopt the same orchestrator+subagents execution mode. Backend
   Architect for the EffectChain ↔ OutOfProcessPluginInstance wiring.

### S3 acceptance criteria (V7 plan lines 533 + 487-503)

- `EffectChainHost` (or equivalent) owns one `OutOfProcessPluginInstance`
  per active plug-in slot on a Constituent's `EffectChain`.
- `Bus::process` (or `OutputMixer::renderBuffer` step 4) invokes the
  hosted plug-in's audio-thread `tryReadBytes`/`tryWriteBytes` per
  buffer, swapping the M5 inline pass-through with real plug-in dispatch.
- An integration test loads the synthetic CLAP plug-in (already built
  by S2a) into an EffectChain slot, plays stereo audio through it,
  asserts byte-for-byte round-trip.
- Audio-thread surface of OutOfProcessPluginInstance is audited in
  `docs/RT_SAFETY_CONTRACT.md §6` (S2d added the §5 measured-latency
  note; S3 adds the table row).
- All existing 364 ctest cases still pass; new integration test added.
- Test count target: ~365-368.

### M7-era decisions locked (S3 must preserve)

1. **POSIX-only scope** (macOS + Linux; Windows defers).
2. **Host binary stays JUCE-free.** CLAP + SHM primitives are in
   `Sirius::Core` (JUCE-free) for exactly this reason. **DO NOT**
   link the host binary against `Sirius::Engine` or `Sirius::Host`.
3. **`OutOfProcessPluginInstance` public API is byte-oriented** —
   `sendBytes`/`readBytes` signatures unchanged through S2. S3 adds
   audio-thread `tryReadBytes`/`tryWriteBytes` siblings (zero-wait,
   noexcept).
4. **`isRunning()` stays non-const + silently reaps + clears
   `childPid_`.** Don't restore the const.
5. **Zero allocation / zero locks on the audio-thread side of the
   rings.** The SPSC primitive is wait-free; SHM regions are created
   at construction time (message thread). S3 must NOT introduce any
   audio-thread allocation when wiring through EffectChain.
6. **`NotificationBus` (M6) is the engine→UI truthfulness channel.**
   When S4 wires plug-in watchdog events, they post into the existing
   bus. S3 does not need to wire it (no watchdog events yet).
7. **Drop-NEW overflow policy** for SPSC rings — if the engine→host
   ring is full, `sendBytes` (and S3's `tryWriteBytes`) returns false
   and the caller surfaces it. Watchdog work in S4 handles the back-
   pressure semantics.
8. **macOS shm_open name length cap (31 chars including leading slash).**
   Current name layout is `/sirius.<instanceId>.<suffix>` with `suffix`
   ∈ {`e2h`, `h2e`}. `<instanceId>` budget is ~18 chars. Long ids must
   be hashed by the caller; S3 should add a `hashInstanceId(std::string)
   -> std::string` helper if any Constituent path generates ids > 18.
9. **`PluginIpcMessage::monotonicNs` will become LMC-domain in S3.**
   Reinterpret the slot when wiring the audio thread; document the
   semantic shift in the header.

### Carryover from M6 + earlier still unresolved (M7 doesn't touch)

- **ProcessedRoute STILL passing empty span to DirectLayer.** Not M7's
  problem (covered by OutputMixer's produced-mix path).
- **Manual operator-launch eyes-on of M6 notification surface.**
  Operator launched the app during the M7 S1 chat; visual verification
  status from operator's side is unknown to Claude. Not blocking M7.
- **Operator-side CI signing handoff (3 secrets pending).** Does not
  block M7 work. Will block the M7 milestone close (S10+) when the
  binary signing has to cover `sirius_plugin_host` too — coordinate
  with the signing-secrets handoff before then.

---

## HISTORICAL — M6 close + M7 handoff (superseded 2026-05-18 — M7 S1 now shipped)

The original M7 first-moves + M6-era constraints follow. M6-era
constraints #1-#8 + M5-era + M4-era ARE STILL LOAD-BEARING for
M7 S2+; only the S1-specific first-moves are now history.

### M7 first moves (superseded — S1 shipped)

**M6 of the V7 alignment plan is fully on `origin/master`.** HEAD is
`a5b86bb`. Three session commits shipped:

| SHA | Subject |
|---|---|
| `7e6b3e6` | M6 Session 1 — NotificationBus + per-category SPSC + tests |
| `a9567f6` | M6 Session 2 — wire NotificationBus into AudioCallback + InputMixer + TapeWriter + MainComponent drain |
| `a5b86bb` | M6 Session 3 — Preparation-tab notification surface + rolling history + integration tests |

Test count: **349/349** green (was 336 at M5 close; +13 across M6 —
8 for S1, 1 for S2, 4 for S3). Full `cmake --build` clean. Manual
operator-launch verification of the notification surface is the one
remaining M6 verification step (deferred from automated smoke — the
spec called for "launch app, trigger an overload, confirm notification
appears" which is fragile to drive via macOS AX).

**Execution mode used:** orchestrator+subagents (Backend Architect
implementer per session; spec review + code review per session; final
milestone review at end). The final milestone review caught a real
layout bug — `kPreparationTopAndBottomReservedPx = 180` was
double-counting the notifications row's height (the bottom-anchored
stack had already been `removeFromBottom`ed before the clamp executed,
so the 80→180 bump silently stole ~100 px of timeline headroom on
small windows). Reverted to 80 + renamed to
`kPreparationTreeMinHeightPx` before the close-out commit. Both per-
session reviews caught real polish items: drop-new overflow doc + a
static_assert pinning kCategoryCount; channel-id in TapeWriter failure
messages; deferred-TapeWriter-injection comment.

### First moves for the M7 chat

M7 is **the largest single piece of architectural work in the plan**
(V7 plan line 489): out-of-process plug-in hosting. Standalone
`sirius_plugin_host` executable, shared-memory SPSC IPC rings,
watchdog, supervisor, GUI host process with platform-specific window
embedding (macOS first per the platform-order rule), parameter
marshalling. **No in-process fallback** per V7 §9.1.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and read
   **M7 in full** (starts at line 487). Sessions 1-12 (estimated)
   break-out is at lines 530-535: S1 = host executable + stdin/stdout
   identity; S2 = shared-mem + SPSC rings + LMC timestamps; S3 = engine-
   side OutOfProcessPluginInstance wired through OutputMixer; S4-N =
   watchdog + supervisor + macOS GUI embedding + platform conditionals.
3. Read V7 §9.1 in the white paper for the full IPC + watchdog spec.
4. M7 is significantly larger than M3-M6. **Sessions 1-3 are the
   foundational layer** (executable, IPC, engine integration);
   subsequent sessions add the watchdog + supervisor + GUI embedding.
   Treat the first chat as Sessions 1-3 only; subsequent chats handle
   later sessions. The V7 plan acknowledges "Sessions 4-N (estimated)"
   without pinning specifics — let the brainstorm + first 3 sessions
   inform the rest.
5. Brainstorm pass for M7 before code. Likely open questions:
   - **CLAP-first format choice.** M8 spec says CLAP is the first
     hosted format (cleanest API, no legacy baggage). VST3 + AU follow
     in M20 + M21. M7 itself is format-agnostic — verify the host
     binary's plug-in-loading layer is abstracted enough that adding
     VST3/AU later is a new translation unit, not a rewrite.
   - **macOS sandbox entitlements.** The signed CI workflow
     (`ci-macos-signed.yml`) needs updating to sign `sirius_plugin_host`
     too; shared-memory IPC requires careful entitlement design under
     `com.apple.security.app-sandbox` + `com.apple.security.cs.allow-shared-memory`.
     The CI signing handoff (3 secrets still pending per §"CI signing
     handoff" below) intersects this — M7 may push the operator-side
     CI work to actually finish before the M7 milestone closes.
   - **Wet-capture pre-allocation budget.** Per the plan line 551, M8
     commits the wet-capture budget; M7 only allocates input/output
     rings. Verify M7 doesn't accidentally consume the M8 budget.
   - **Host process lifecycle: spawn-per-instance vs pool.** V7 §9.1
     prescribes per-instance spawn. Confirm this isn't relitigated in
     brainstorm.
6. Adopt the same orchestrator+subagents execution mode. M7's V7 plan
   suggests Backend Architect for the IPC ring shape; Security Engineer
   for sandboxing review; Performance Benchmarker for round-trip
   latency. Use the specialists when their scope matches.

### M6-era decisions that constrain M7 (DO NOT "fix" without operator approval)

These are NEW constraints landed in M6; combine with the M5-era +
M4-era constraints further down (still all load-bearing).

1. **`NotificationBus` is the engine→UI truthfulness channel.**
   `post(level, category, const char*) noexcept` is allocation-free,
   wait-free on the audio-thread path via per-category SPSC rings (256
   entries each). M7's watchdog + supervisor will post `PluginEvent`
   notifications when a host process misses deadlines, crashes, or
   restarts — use the existing bus, do NOT spin a parallel signal
   channel.
2. **Drain is message-thread-only + must pre-reserve** to
   `kCategoryCount * kRingCapacity = 2304` to honor the bus's
   `bad_alloc` contract (drain is NOT `noexcept`). MainComponent
   already does this at `app/MainComponent.cpp:643`.
3. **Bus is owned by `MainComponent`; set-once on collaborators**
   BEFORE `audioDeviceAboutToStart` (same pattern as DirectLayer/
   OutputMixer per M5 #8). M7's `OutOfProcessPluginInstance` will
   likely follow the same: MainComponent (or a per-Constituent owner)
   constructs + injects.
4. **Overflow policy is drop-NEW** (not drop-oldest as plan line 479
   suggested) — the only SPSC-correct choice since the producer can't
   touch the consumer's head. `overflowCount(Category)` exposes the
   per-category counter. M7's IPC ring should follow the same policy.
5. **`drain()` signature is out-param `void drain(vector&)`**, NOT
   return-by-value (deviation from plan line 444, documented at
   `engine/include/sirius/NotificationBus.h:139-151`).
6. **`TapeWriter` has `setNotificationBus` wired but is NOT currently
   owned by `MainComponent`.** When TapeWriter joins the owned app
   graph (M11 SAF wiring is the likely trigger), inject the bus per
   the comment at `app/MainComponent.cpp:537-541`. M7 doesn't touch
   TapeWriter.
7. **AudioCallback posts `DeviceEvent` only from
   `audioDeviceAboutToStart`/`audioDeviceStopped`** (JUCE setup
   callbacks, not the render thread). No render-thread post sites yet
   from AudioCallback itself. M7's audio-thread plug-in dispatch will
   add render-thread posts (`Warning, PluginEvent, ...`) for
   watchdog misses — that IS allowed, post is noexcept.
8. **`kNotificationHistorySize = 20` in MainComponent**; M22 redesigns
   the UI surface. M7 may add more notification post sites that
   surface here — fine.

### Carryover from M5 NOT resolved (still deferred — M7 doesn't touch)

- **ProcessedRoute STILL passing empty span to DirectLayer.** M5+M6
  did not unblock this. Deferral comment lives at
  `audio/src/AudioCallback.cpp:66-77`. Not urgent — OutputMixer's
  produced-mix path (Step 4) covers the surface DirectLayer would have
  used. M7 doesn't need to fix this.
- **Manual operator-launch verification of M6 notification surface**
  is the one remaining M6 verification step (deferred from automated
  smoke per the AX-fragility constraint from M4 GUI smoke work).
  Not blocking M7; operator should launch the .app once at their
  convenience to eyes-on the surface.

---

## HISTORICAL — M5 close + M6 handoff (superseded 2026-05-18 — M6 now complete)

The original M6 first-moves + M5-era constraints follow. M5-era
constraints #1-#9 ARE STILL LOAD-BEARING for M7+; the rest is history.

### M6 first moves (superseded)

**M5 of the V7 alignment plan is fully on `origin/master`.** HEAD is
`6cd3810`. Three session commits shipped:

| SHA | Subject |
|---|---|
| `2eb296c` | M5 Session 1 — ChannelStrip<Audio> + InputMixer input-side gain/pan |
| `25da0ff` | M5 Session 2 — Bus + send/return + master bus + OutputMixer surfaces |
| `6cd3810` | M5 Session 3 — OutputMixer::renderBuffer + Bus::process audio-thread wiring + AudioCallback Step 4 |

Test count: **336/336** green (was 310 at M4 close; +26 across M5 — 6
for S1, 14 for S2, 6 for S3). Full `cmake --build` clean. `bash bash/autotest.sh`
not re-run this session (S1–S3 are headless code changes; the M4 close-out
GUI smoke is still the last operator-verified launch).

**RT-budget result for the 32-channel × 8-bus configuration:** 17.5 µs
max elapsed on the dev machine. At 48kHz/256-sample buffer (5.33 ms
budget), that's **0.33% of buffer time** — well inside any realistic
audio-thread budget. Hidden `[output-mixer][.rt-smoke]` Catch2 tag,
matches the DirectLayer pattern.

**Execution mode used:** orchestrator+subagents (Backend Architect
implementer per session; spec review + code review per session; final
milestone review at end). The final milestone review caught a stale
ProcessedRoute deferral comment + a `routeChannelToOutput` dead-code
placeholder + a missing `static_assert` linking
`OutputMixer.cpp:kMaxBlockSamples` to `Bus::kMaxBusMixSamples` — all
folded into the S3 commit before push, so HEAD is the M5 close.

### First moves for the M6 chat

M6 ships the **NotificationBus** — V5 §8.6 engine↔UI truthfulness
channel. Wait-free SPSC per category for audio-thread posts (failure
events, accessibility cues, capacity warnings, partition events);
message-thread drain renders into a minimal Preparation-tab UI
surface (M22 redesigns the surface — M6 is just the plumbing).

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and read
   **M6 in full** (starts at line 435). Sessions 1-3 break-out: S1 =
   `NotificationBus.{h,cpp}` + per-category SPSC + unit tests; S2 =
   wire into existing emitters (M1 device events, M3 tape rotation,
   M5 overload); S3 = Preparation-tab UI surface + smoke test.
3. Read V5 §8.6 in the white paper for the category enum + ordering
   semantics, and V5 §17.9 for failure-event categories.
4. Brainstorm pass for M6 before code. Likely open questions:
   - **Per-category ring depth.** Plan says 256 entries per category
     as a starting cap. Audio-thread is wait-free but message-thread
     drain cadence matters — if drain is on the 30 Hz UI timer, that's
     8.5 s of buffer at full posting before overflow. Probably fine
     for "rare" notifications; verify the categories that could spam
     (overload reports could fire once per buffer at 44.1 kHz / 256
     samples = ~170 Hz; ring fills in 1.5 s if drain pauses).
   - **Where the engine-side post sites live.** S2 wires existing
     emitters: M1's `OverloadProtection::reportLoad(1.0)` becomes a
     `notificationBus_->post(Warning, CpuPressure, "audio thread
     missed deadline")`; M3's `TapeWriter` flush failure becomes a
     `post(Error, DiskPressure, ...)` etc. List the existing
     emit sites first, decide the ones M6 wires now vs defers.
   - **Atomic-snapshot vs ring per category.** The plan calls for
     SPSC rings per category. If a category never has >1 entry
     pending at a time (e.g. `ClockEvent` with infrequent device
     resyncs), a single-cell atomic snapshot would be cheaper. Worth
     a one-question pass — but defaulting to ring keeps the contract
     uniform across categories, which is probably the right call.
5. Adopt the same orchestrator+subagents execution mode. Per-session
   spec review + code review caught real issues across all three M5
   sessions (`static_assert` cross-file linking, deep-const escape
   hatch on `ChannelStrip::process`, jassertfalse fail-loud asserts,
   tautological tests deleted). M6's V7 plan suggests the same mode.

### M5-era decisions that constrain M6 (DO NOT "fix" without operator approval)

These are NEW constraints landed in M5; combine with the M4-era
constraints further down (still all load-bearing).

1. **`ChannelStrip<SignalType>` is the per-modality processing template,
   inheriting `ProcessingChain`.** `ChannelStrip<Audio>` is the only
   real one; `<Midi>`/`<Video>`/`<File>` are stubs in `ChannelStrip.h`.
   The M3-era `AudioChain` is REMOVED — `makeProcessingChain(Audio)`
   returns `ChannelStrip<Audio>`. M9/M12/M13 will rename
   `MidiChain`/`VideoChain`/`FileChain` to their `ChannelStrip`
   counterparts (left as-is in `ProcessingChain.h` for now — M6 does
   NOT need to touch this).
2. **`OutputMixer` comes up empty by default.** No channels auto-register
   in M5 (operator UX for mixer config is M22+). `renderBuffer` early-
   returns on `channels_.empty()`. M6's NotificationBus integration
   does NOT need to register OutputMixer channels.
3. **Per-OutputChannel audio source in M5 is `inputChannelData[OutputChannelId.value()-1]`.**
   One-to-one with input device channels. This is a placeholder until
   Constituent rendering lands (post-M6). Don't take a hard dependency
   on this mapping in M6.
4. **`Bus::process` zeroes its own `mixBuffer_` after the additive
   write to output.** `mixBuffer_` is `mutable` because the bus
   accumulation path (via `mixBufferChannel(int) const noexcept`) is
   called from `OutputMixer::renderBuffer`, which is `const`. M6's
   NotificationBus post-from-audio-thread interface will likely use the
   same `mutable` + `const post()` pattern.
5. **Non-master buses' `mixBuffer_`s are zeroed INLINE in
   `OutputMixer::renderBuffer` step 3** (not via `Bus::process`).
   Only the master bus invokes `Bus::process` (at step 4). When M7
   wires real plugin invocation through `EffectChain`, the inline
   path must become a real `Bus::process` invocation; flag for M7's plan.
6. **`BusId{0}` is the implicit master bus, auto-created in OutputMixer
   ctor, not removable.** `addBus` returns `BusId{1+}` for aux buses.
   M6 doesn't add buses.
7. **`EffectChain` is held on `Bus` but not invoked in M5.** Real
   plugin dispatch lands with M7 (out-of-process hosting). M6 does
   not need to do anything with `EffectChain`.
8. **`AudioCallback` orchestrates a 6-step audio path:** 1 zero
   outputs → 2 InputMixer → 3 DirectLayer (monitoring-gated) → 4
   OutputMixer (ungated) → 5 Lmc → 6 publish elapsed. The class-level
   threading-contract docblock at `AudioCallback.h:45-57` documents
   the configure-before-audio-starts invariant for every collaborator
   setter. M6's NotificationBus will likely become a new
   collaborator via `setNotificationBus(NotificationBus*)` — follow
   the same set-once pattern.
9. **`OutputMixer*` in AudioCallback is `const OutputMixer*`**
   (matches `const DirectLayer*` per M4 constraint #4). `renderBuffer`
   is `const noexcept`. If M6's NotificationBus has a `post()` that
   can fire from any thread including audio, and if AudioCallback
   holds it as a collaborator, it'll likely be `NotificationBus*`
   non-const (post mutates the ring), with `post` itself being
   `const noexcept` from the bus's POV (mutates only internal
   atomic-published ring state). Decide the const-ness in S1
   brainstorm.

### Operator-side TODOs for M6

1. **CI signing secrets** (still operator-pending — unchanged from M4).
   Three secrets remain. See "CI signing handoff" section further down.
2. **Decide per-category ring depth** (see brainstorm open questions).
3. **Decide which existing emit sites M6 wires now vs M11+ defers.**

### Carryover from M5 NOT resolved (defer to M6 or later)

- **ProcessedRoute is STILL passing empty span to DirectLayer.** M5
  was supposed to unblock this once `ChannelStrip<Audio>` is real,
  but `InputMixer` doesn't expose the post-strip float buffer publicly
  (applies strip into a private scratch then memcpys to TapeWriter).
  The deferral comment at `audio/src/AudioCallback.cpp:66-72` now
  documents this honestly. **M6 may unblock** by adding either an
  audio-thread getter to InputMixer (post-strip float view) or a
  parallel output pointer for the audio callback to capture — but
  it's not urgent; OutputMixer's own processed path (Step 4) covers
  the produced-mix surface.
- **AudioCallback diagnostics pane height** (60 → 84 px after M1 S3
  Load line). If M6's notification UI sits on the Preparation tab,
  it'll likely need another bump or a switch to dynamic height — flag
  to the M6 S3 implementer when the surface lands.

---

## HISTORICAL — M4 close + M5 handoff (superseded 2026-05-18 — M5 now complete)

The original M5 first-moves and M4-era constraints follow. M4-era
constraints #1-#7 ARE STILL LOAD-BEARING for M6+; the rest is history.

**M4 of the V7 alignment plan is fully on `origin/master`.** HEAD is
`d0aa45f`. Four commits shipped this session, all green:

| SHA | Subject |
|---|---|
| `70f8030` | M4 Session 1 — DirectLayer registry + OutputChannelId + dense generation handles |
| `0130a5f` | M4 Session 2 — DirectLayer::routeBuffers audio-thread additive routing |
| `3dff703` | M4 Session 3 — AudioCallback orchestrates InputMixer + DirectLayer; MainComponent owns mixers |
| `d0aa45f` | docs: M4 close-out — RT_SAFETY_CONTRACT §6 rows for DirectLayer::routeBuffers + InputMixer::processBuffer + AudioCallback orchestration update |

Test count: **310/310** green (was 293 at M3 close; +17 across M4).
Full 4-phase `bash bash/autotest.sh` green (Phase 4 GUI smoke flaked
twice on cold-start runs; passed cleanly on retry — known
Accessibility-timing transient, unrelated to M4 changes).

**Execution mode used:** subagent-driven (one implementer + spec
review + code review per session, then a final milestone review).
Per-session reviews caught: dense-storage vs tombstone-walk RT issue
(S1 fix-up), `routeBuffers` const-correctness + RT-smoke flake risk
(S2 fix-up), `audioDeviceAboutToStart` allocation in `vector::assign`
(S3 fix-up). The final milestone review caught the
`RT_SAFETY_CONTRACT.md §6` table missing rows for the new
audio-thread classes — fixed in `d0aa45f` before declaring M4 done.

### First moves for the M5 chat

M5 ships the **OutputMixer expansion** — V3 Step 7 + decision #57:
per-channel strips with gain/pan/EQ/dynamics, session-level effect
buses, send/return architecture, master bus, real `OutputMixer::render_buffer`
on the audio thread. **Plus** the input-side `AudioChain` becomes
real (no longer no-op) — `ChannelStrip<SignalType::Audio>` is the
shared implementation between input and output mixer channels.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and read
   **M5 in full** (starts at line 376). Sessions 1-3 break-out is at
   the end of the M5 section (Session 1 = ChannelStrip template +
   OutputMixer channel registration; Session 2 = Bus + sends; Session
   3 = `render_buffer` audio-thread wiring + integration test).
3. Read V3 transition guide §2.4 / §3.3 / §3.4 for the channel-strip
   / bus / send topology.
4. Brainstorm pass for M5 before code. The plan body is dense; key
   open questions worth surfacing to the operator:
   - **ChannelStrip inheritance vs composition** — plan amendment §3
     in `docs/superpowers/specs/2026-05-18-m3-design.md` says either
     `ChannelStrip<Audio>` inherits `ProcessingChain` (rename — preferred)
     or `AudioChain` delegates to a held `ChannelStrip<Audio>`
     (composition — fallback). Pick one before Session 1.
   - **`OutputChannelId` promotion** — M4 ships it as a strong-typed
     int wrapper in `engine/include/sirius/Channel.h`. M5 needs to
     decide whether the promotion adds methods to the wrapper in-place
     or whether `OutputMixer::add_channel` returns a NEW richer type
     (`OutputChannel` value object?) that holds the id plus the strip
     reference. The V7 plan acceptance criterion ("`add_channel(...)`
     returns `OutputChannelId`") suggests the wrapper stays, methods
     get added.
   - **OutputMixer slot in AudioCallback** — Session 3 of M4 wired the
     callback's 5-step body (zero → InputMixer → DirectLayer → LMC →
     elapsed). OutputMixer is currently a held-but-not-invoked
     `(void) outputMixer_;` marker at line ~160 of
     `audio/src/AudioCallback.cpp`. M5 Session 3 turns that marker
     into the real `render_buffer` call — additive into the same
     output buffers DirectLayer also writes to. Operator should know
     the M5 callback grows from 5 steps to 6 (or DirectLayer + OutputMixer
     get fused as one step).
5. Adopt the same subagent-driven execution mode that worked for M3+M4.
   Plan-write → spec-review → code-review per session; final-milestone
   review at the end. **The execution mode in the V7 plan is
   `orchestrator+subagents` with Performance Benchmarker for the
   32-channel/8-bus RT-budget test and Backend Architect for the
   channel→bus→master DAG audit.** Worth using those specialists
   instead of generic-purpose for the matching sessions.

### M4-era decisions that constrain M5 (DO NOT "fix" without operator approval)

1. **`OutputChannelId` lives in `engine/include/sirius/Channel.h`**
   alongside `ChannelId` / `InputId`. M5 promotes by adding methods
   in-place — do NOT move it to a new header.
2. **AudioCallback orchestrates the audio path; InputMixer / OutputMixer
   / DirectLayer do not call each other.** AudioCallback owns the
   sequence "for each input → InputMixer per channel → DirectLayer
   routeBuffers → OutputMixer render_buffer (when M5 makes it real) →
   LMC → elapsed publish." M5 must NOT add `setOutputMixer(...)` to
   InputMixer or `setOutputMixer(...)` to DirectLayer.
3. **MainComponent owns mixers + DirectLayer via `std::unique_ptr`,
   injects raw pointers into AudioCallback set-once on the message
   thread.** Matches the M1 Session 3 ASRC pattern. M5's Bus
   instances should follow the same shape (MainComponent owns; raw
   pointer injection if they need to be visible across components).
4. **DirectLayer is `const DirectLayer*` because `routeBuffers` is
   `const`.** If `OutputMixer::render_buffer` can be `const` (which
   it should — render is a function of state, not a state mutator),
   do the same: `const OutputMixer*` in AudioCallback. If not, document
   why not.
5. **Scratch vectors in AudioCallback are pre-sized to
   `kMaxScratchChannels = 32` in the constructor; `audioDeviceAboutToStart`
   only records active counts.** Zero allocation in the
   message-thread-on-audio-boundary path. If M5 OutputMixer needs its
   own per-buffer scratch (e.g. send-bus intermediate buffers), use
   the same pattern — pre-allocate in constructor, record active
   sizes in `audioDeviceAboutToStart`, only index in the callback.
6. **Threading contract: configure-before-audio-starts.** Documented
   in `DirectLayer.h` and inherited by AudioCallback. No atomic-snapshot
   publish on route mutation. M5 inherits the same — bus/strip
   configuration happens before the audio thread reads them, period.
   When the operator-facing mutation UI lands (post-M5), AudioCallback
   will need a temporary stop-callback-mutate-restart guard or a real
   lock-free publish; defer the decision until the UI is real.
7. **ProcessedRoute is wired but not exercised in M4** — the
   `processedChannels` span in `DirectLayer::routeBuffers` is passed
   empty from AudioCallback (`audio/src/AudioCallback.cpp` line ~103).
   Reason: M3's `InputMixer::processBuffer` writes byte-serialized
   output to a TapeWriter queue and does NOT expose a post-processing
   float buffer. **M5 unblocks this**: once `ChannelStrip<Audio>` is
   the concrete `Channel::processing` body, InputMixer can publish
   a per-channel post-processing float buffer that AudioCallback
   populates into the `ProcessedChannelBufferView` scratch and passes
   to DirectLayer. Add this as M5 Session 3 work — it's a natural fit
   alongside `render_buffer` wiring. Without it, ProcessedRoute
   registration is allowed but routes nothing.

### Known M4 deviations + minors deferred to later milestones

These are real but intentionally not fixed in M4. Don't pick them up
opportunistically in M5 unless they overlap with M5 scope.

- **`AudioCallback.h` lacks a threading-contract note** matching
  `DirectLayer.h`'s. The configure-before-start assumption is in the
  setters' doc comments but not the class-level block. Add the
  paragraph during M5 Session 3 when `setOutputMixer` joins
  `setDirectLayer` in the same neighborhood.
- **No regression test for the InputMixer-no-registered-channels
  no-op path** on the audio thread. M4 default config exercises it
  (MainComponent doesn't register any InputMixer channels) but
  nothing asserts it stays a no-op if the early-return in
  `InputMixer::processBuffer` is ever changed. M5 will register
  channels for real (gain/pan input-side processing) — when it does,
  add an assertion that unregistered channels still no-op gracefully.
- **No test for `DirectLayer::removeRoute` mid-audio.** Currently
  illegal per the threading contract, so untestable. When mutation-
  during-audio becomes a real surface (post-M5), add the test.
- **DirectLayer's `[.rt-smoke]` test is hidden by default** (Catch2
  hidden-tag convention) — runs only on explicit filter via the
  test binary, not `ctest`. Acceptable for a performance smoke;
  document if M5 adds a similar smoke for `render_buffer`.

### RT-safety invariants M5 OutputMixer must preserve

- Zero allocation, zero locks, zero I/O, zero logging on the audio
  thread. `noexcept` on every hot-path function.
- `render_buffer` should be `const` and audio-thread-only.
- Channel → bus → master DAG traversal must be bounded. The V3
  decision caps buses at 64; channels are typically <32. Worst case
  ~2048 send operations per buffer — well within budget at modern
  CPU.
- Bus effect chains (`EffectChain` is reused from core/) — verify
  each effect's audio-thread call is allocation-free. M5 only ships
  identity-EQ + identity-dynamics stubs; real DSP is a deferred
  sub-milestone.
- Per `docs/RT_SAFETY_CONTRACT.md §6`, every new audio-thread class
  gets a row in the table BEFORE the milestone closes. M4 missed
  this and had to backfill — don't repeat the mistake.

### Operator-side TODOs for M5

1. **CI signing secrets** (still operator-pending — does NOT block
   any V7 alignment work, but the auto-testing milestone won't go
   fully green on CI until done). See the "CI signing handoff" section
   further down. Three secrets remain.
2. **Decide ChannelStrip inheritance vs composition** (see brainstorm
   open question above).
3. **Optional:** Use Performance Benchmarker subagent for the
   32-channel/8-bus RT-budget test in M5 Session 3 (per the V7 plan's
   recommended execution mode).

---

## HISTORICAL — M3 close-out + M4 handoff (superseded 2026-05-18 — M4 now complete)

**M3 shipped on `origin/master` HEAD `b395f2e`.** Six commits, test count 279 → 293.

| SHA | Subject |
|---|---|
| `58a6d7d` | M3 Session 1 — ProcessingChain + ChannelDefaults + InputDescriptor flags + Channel ctor |
| `aba4e0e` | M3 Session 2 — TapeWriter + InputMixer::processBuffer real bodies |
| `ada4858` | M3 Session 2 fix — keep SiriusEngine public API JUCE-free (TapeWriter PIMPL) |
| `f4ef59f` | M3 Session 2 fix — sampleCount→payloadByteCount + flushChannel preconditions + OverloadProtection::reportLoad noexcept |
| `94aed46` | M3 Session 3 — finalizeChannel + NonDestructive params + setInputDefaults end-to-end |
| `b395f2e` | M3 follow-up — TapeWriter flushChannel prompt wake on empty queue + simplify predicate to fail loud in NDEBUG |

M3-era decisions still load-bearing in M5:
- `TapeWriter` takes `std::chrono::milliseconds`, NOT `CapabilityTier&` (engine can't depend on app layer).
- `TapeMode` lives in `core/` (because `ChannelDefaults` needs it).
- InputMixer collaborator setters are set-once on the message thread; never null-checked at every audio-thread call.
- Engine public API is JUCE-free via PIMPL where filesystem types are needed.
- `flushChannel` has a single-caller precondition (asserted in debug, blocks-loud in NDEBUG).
- `TapeWriteMessage::payloadByteCount` (not `sampleCount`); `lmcTime` is `Rational(0)` until per-channel sample-rate context exists.

Deferred from M3 to specific later milestones:
- `touchParamsPartial` silent-fail if `setChannelTapeMode(NonDestructive)` before `setTapeWriter` — flagged for M11.
- `touchParamsPartial` holds `stateMutex_` across `std::ofstream` open — negligible, defer.
- `finalizeChannel` doesn't `removeChannel` — revisit when iterating channels per buffer (M5+ may hit this).
- `[finalize]` test doesn't cover no-messages-enqueued path — cheap insurance missing.
- No mechanical RT-safety counter on `processBuffer` — M11+ may add `RtSafetyHarness`.

---

## HISTORICAL — M2 close-out + M3 handoff (superseded 2026-05-18 — M3 now complete)

**M2 of the V7 alignment plan is fully shipped on `origin/master`.**
All three sessions are pushed (HEAD `f1b4d58`):

- `ec1b5d9` — Session 2: type shapes (SignalType, TapeMode, Channel,
  InputMixer/OutputMixer skeletons)
- `9edd311` — Session 2 handoff continue.md
- `f1b4d58` — Session 3: skeleton tests for the new types

Test count is **279/279** (was 270; +9 cases from Session 3: 2
SignalType, 5 Channel/TapeMode/InputId/ChannelId, 1 InputMixer, 1
OutputMixer). Full 4-phase `bash bash/autotest.sh` green; Phase 4 GUI
smoke flaked once on a cold build but passed on retry — known
Accessibility-timing transient, unrelated to type-shape additions.

### First moves for the M3 chat

M3 is **the first behaviour-bearing milestone in Part B** — the
mixer skeleton bodies that currently assert-false get real
implementations, and the first `Tape<T>` instance in product code
lands. This is a *real* session, not a rename or scaffolding pass.

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` and read
   the **entire M3 section** (lines ~263 onward — "Channel-driven tape
   allocation + channel-layer processing chains + per-input flags").
   M3 is significantly larger than M2: it changes `Channel`, adds
   `ProcessingChain`, adds `TapeWriter` + lock-free queue, extends
   `TapeStore` with `appendBytes`, and adds three flags to
   `InputDescriptor`. Files-touched list is long.
3. **Brainstorm first, then propose a sub-session break-out.** M3 is
   large enough that it deserves a `superpowers:brainstorming` pass
   before any code, and a retro-split into 3-or-4 sessions (matching
   the M2 pattern) before execution. Do not dive straight into
   implementation — the M2 break-out earned its keep, M3's will too.

   The two would-be operator questions about audio-thread contract and
   parameter-tape format are **already resolved** (decisions locked
   2026-05-18 — see "Operator decisions locked for M3" below). The
   brainstorm should focus on remaining open shapes — `ProcessingChain`
   parameterization by `SignalType`, the audio-thread / writer-thread
   handoff struct, and how `InputDescriptor`'s three new flags
   (`rawDirectMonitor`, `enabled`, `defaults`) interact with the
   already-shipped `InputMixer::setInputRawDirect` / `setInputEnabled`.
4. Read the V3 transition guide §2 (the same lines used in M2) plus
   §2.4 / §3.3 / §3.4 for the channel/tape/effects topology that M3
   makes real.
5. Read what M2 left in place:
   - `engine/include/sirius/InputMixer.h` — method names are PascalCase
     (`registerInput`, `addChannel`, `setChannelTapeMode`,
     `processBuffer`); placeholder args are `/* M3: ... */` comments
     in the signatures. M3 turns those comments into real types.
   - `engine/src/InputMixer.cpp` — every body
     `assert(false && "M3-M5 stub")`. M3 replaces every assert with a
     real body — don't leave any stubs.
   - `engine/include/sirius/Channel.h` — currently 4 fields
     (`id`, `signalType`, `source`, `tapeMode`). M3 adds
     `ProcessingChain processing` and a destinations container.
6. Plan break-out for M3 is **not yet sub-sessioned**. The M2 break-out
   (Sessions 1/2/3) was added retroactively after brainstorming with
   the operator; do the same for M3 — propose a 3-or-4-session split
   in the first chat, get operator agreement, then execute.

### Operator decisions locked for M3 (2026-05-18)

These came out of the M2-close-out conversation and remove two would-be
brainstorm questions:

1. **SPSC overflow on `processBuffer`:** when the audio-thread → writer-
   thread queue is full, the audio thread **drops the buffer and
   reports overload** via `OverloadProtection.reportLoad(1.0)`. Matches
   the M1 Session 3 pattern (overload is the existing surface for
   "audio thread couldn't keep up"). Never block on the audio thread;
   no silent drops. The diagnostics pane already shows shed-count from
   OverloadProtection, so the operator-visible signal is in place.
2. **`NonDestructive` parameter-tape format:** JSONL, same shape as the
   existing `ParameterAutomation` tape. Keeps SAF consistent and lets
   the same parameter-merge/diff utilities work across both tape kinds.
   Defer any "binary format would be smaller" optimization to a future
   milestone — premature today.

Both decisions belong in the M3 Risks section of the V7 alignment plan
when M3 lands; capture them there as part of Session 1's commit.

### M2 Session 3 — what landed and pushed (commit `f1b4d58`)

5 files changed, 223 insertions, 1 deletion:

- `tests/SignalTypeTests.cpp` (new) — exhaustive `signalTypeOf` mapping;
  `static_assert`s at file scope plus runtime `CHECK`s in 2 TEST_CASEs.
  Tag `[signal-type]`.
- `tests/ChannelTests.cpp` (new) — `InputId` / `ChannelId` strong-typing
  (runtime `CHECK`s only; the house IDs have non-constexpr accessors so
  compile-time asserts on `value()` / `==` don't work — see deviation
  note below). `Channel` aggregate brace-init; `SignalType × TapeMode`
  combinations all build; `TapeMode` three-case distinctness. Tag
  `[channel]` + sub-tags `[input-id]` / `[channel-id]` / `[tape-mode]`.
- `tests/InputMixerTests.cpp` (new) — single TEST_CASE confirming
  default ctor + dtor don't crash; `static_assert` that
  `is_default_constructible_v<InputMixer>` holds. **No setter / no
  `addChannel` / no `processBuffer` calls** — every body still asserts
  false per the M2 Risks note. Tag `[input-mixer]`.
- `tests/OutputMixerTests.cpp` (new) — same shape as InputMixerTests.
  Tag `[output-mixer]`.
- `tests/CMakeLists.txt` (modified) — appended the 4 new files to the
  `SiriusTests` source list.

### Session 3 plan deviations to carry forward into M3

1. **Static_assert on `InputId.value()` / `==` doesn't work.** The
   `value()` accessor and `==`/`!=` operators on `InputId` / `ChannelId`
   aren't `constexpr` — matched the house `TapeId` / `ConstituentId`
   pattern exactly, which has the same half-measure (constexpr ctor,
   non-constexpr accessors). Initial Session 3 draft had
   `static_assert(InputId(1).value() == 1)`; clang correctly rejected
   it. Removed those and kept runtime `CHECK`s. If M3+ wants
   constexpr-throughout for the house IDs, that's a project-wide
   refactor (touches TapeId, ConstituentId, InputId, ChannelId), not
   an M3-local change.
2. **Mixer tests are intentionally thin.** Single ctor/dtor test each.
   The plan (line 228) enumerated `InputMixerTests.cpp` /
   `OutputMixerTests.cpp` as deliverables; I shipped them as floors
   (regression catches if a future refactor accidentally makes the
   mixers non-default-constructible). M3 will fill them with real
   behaviour as the assert-false bodies become real implementations.
3. **GUI smoke is flaky on first run after a clean build.** The
   `bash bash/autotest.sh` Phase 4 failed once on a cold build, then
   passed on direct re-run with the same `.app`. Almost certainly an
   Accessibility-permission timing transient (the new app bundle needs
   a moment for AX to attach). Not worth chasing this session. If it
   becomes chronic, the fix is probably a 1–2 s `sleep` after launch
   inside `bash/smoke-persistence.sh` before the first AX call.

### Where M2 acceptance criteria stand (all green)

- [x] Membrane → LatencyTiming rename + namespace flip (Session 1)
- [x] `core/include/sirius/SignalType.h` added (Session 2)
- [x] `signalTypeOf(InputKind)` helper added to InputKind.h (Session 2)
- [x] `engine/include/sirius/InputMixer.h` + `OutputMixer.h` skeletons added — assert-false bodies (Session 2)
- [x] `engine/include/sirius/Channel.h` added with strong-typed `ChannelId` + `InputId` (Session 2)
- [x] `engine/include/sirius/TapeMode.h` added (Session 2)
- [x] Skeleton tests for new types (Session 3 — 9 new test cases)
- [x] Pushed to `origin/master` (Session 3)

M3 (Channel-driven tape allocation + channel-layer processing chains +
per-input flags) is unblocked.

What landed this session (M2 Session 1):

- `engine/include/sirius/LatencyTiming.h`, `engine/src/LatencyTiming.cpp`
  — new, namespace `sirius::latency`. Content is the verbatim former
  Membrane content with namespace + error-message string updates only.
  Two free functions: `inboundCaptureTime`, `outboundPresentTime`.
- `engine/include/sirius/Membrane.h`, `engine/src/Membrane.cpp` —
  deleted. Zero remaining `sirius::membrane::` references in the tree
  (verified by grep).
- `tests/LatencyTimingTests.cpp` — renamed from `MembraneTests.cpp`.
  Catch2 tags updated `[membrane]` → `[latency]`. Test descriptions /
  prose updated. Assertions byte-identical.
- `engine/CMakeLists.txt`, `tests/CMakeLists.txt` — updated.
- `tests/AudioCallbackTests.cpp` — added a `pumpUntilPositive` helper
  so the M1 Session 3 load-publish tests don't depend on a single
  cold-cache callback registering a non-zero tick delta. 1000-iteration
  safety cap so a fundamentally-broken clock can't hang the test.

**Plan deviations from M2 Session 1:**

- The plan predicted "expected hits in `engine/src/RenderPipeline.cpp`,
  `app/MainComponent.cpp`" for `sirius::membrane::` usages. **Zero
  actual hits in either** — `Membrane.{h,cpp}` was already unused by
  product code, only referenced by its own test. The rename surface
  was 3 files, not the 5+ the plan implied. Session 2's "Files
  touched" enumeration is more accurate (no widespread call-site
  rewrites needed).
- `video/include/sirius/FrameMembrane.h` + `video/src/FrameMembrane.cpp`
  are a separate concept (video frame timing) and are **explicitly
  out of scope** for M2 per the plan's narrow wording. Left untouched.

What landed this session (M1 Session 3):

- `audio/include/sirius/AudioCallback.h`, `audio/src/AudioCallback.cpp`
  — added `setAsrcInputs(std::vector<Asrc*>)`, `setAsrcOutputs(...)`,
  `setCalibration(const AudioDeviceCalibration*)` — non-owning,
  message-thread, set-once. New `std::atomic<double> lastCallbackElapsedSec_`
  published via `lastCallbackElapsedSec()`. The callback now wraps its
  work with `juce::Time::getHighResolutionTicks` and stores elapsed
  seconds at the end of every buffer. Buffer body is otherwise
  unchanged — still `memcpy + lmc_->advanceBySamples`.
- `app/MainComponent.{h,cpp}` — owns 2 input ASRCs + 2 output ASRCs
  (`std::unique_ptr<Asrc>`, `maxIoRatio=1.01`, quality from
  `EngineConfig`), one `AudioDeviceCalibration::identity()`, one
  `OverloadProtection`, one `RetroactiveRing<std::uint8_t>{1024}` (the
  `std::uint8_t` is provisional until the real tape-event type lands
  in M3/M4). The 30 Hz `timerCallback` reads
  `audioCallback_->lastCallbackElapsedSec()`, divides by
  `bufSize / rate`, calls `overloadProtection_.reportLoad(fraction)`.
  Diagnostics row in the Preparation pane gained a new `Load: X%
  of budget (shed: N)` line; the label height bumped 60 → 84 px to fit.
- `docs/RT_SAFETY_CONTRACT.md` — four `TBD` rows filled, with notes
  per piece (Asrc held-not-invoked; OverloadProtection driven from
  the message thread via atomic-published elapsed; RetroactiveRing
  explicitly off the audio thread; AudioDeviceCalibration held at
  identity until M8). New post-table paragraph describes the three
  shapes M1 Session 3 introduced.
- `tests/AudioCallbackTests.cpp` — 3 new `[load-publish]` cases:
  elapsed is 0 before any callback; positive (< 1 ms) after one buffer;
  resets to 0 on `audioDeviceStopped()`.

**Operator decisions locked in 2026-05-17 brainstorm (captured in
`~/.claude/plans/read-continue-and-proceed-partitioned-diffie.md`):**

- Q1 — *Strict scaffolding* (option A): engine pieces are reachable
  from the audio path's owners but the callback body remains
  `memcpy + lmc_->advanceBySamples`. ASRC at unity is NOT bit-identical
  (cubic/sinc interpolation), so routing through it would have broken
  the existing identity tests for no M1 benefit.
- Q2 — *Atomic publish, message-thread consumes* (option A). Mid-session
  refinement: the atomic publishes **elapsed seconds**, not the load
  fraction — division moves to the message thread. Net result:
  smaller audio-thread footprint (one `mach_absolute_time` pair + one
  atomic store; no division), testable without a JUCE device mock,
  same operator-facing behaviour.

**Subtle constraints worth carrying forward:**

- The diagnostics pane height grew (60 → 84 px) for the new Load
  line. If M2 adds another diagnostics line it'll likely need
  another bump, or a switch to dynamic height.
- `Asrc` is held by `AudioCallback` but not invoked. Compiler will
  warn about unused if any future cleanup tightens warnings — the
  member is intentional scaffolding; M2-M5 grow the call sites.
- `RetroactiveRing<std::uint8_t>` is a placeholder type parameter.
  The real `T` is the tape-event type that lands with the M3 SPSC
  tape-event queue. Renaming the member at that point is acceptable.

### Where M1 acceptance criteria stand (all green)

- [x] App registers an audio device on startup at user default sample
      rate / buffer size. (M1 Session 1 + 2/2 channel default.)
- [x] `AudioCallback::audioDeviceIOCallbackWithContext` runs identity
      pass-through within buffer budget. (Verified by autotest.)
- [x] LMC sample-clock fed from device callback. (M1 Session 2,
      tests `[sample-clock]`.)
- [x] `Asrc` / `OverloadProtection` / `AudioDeviceCalibration` /
      `RetroactiveRing` wired in. (M1 Session 3 — see RT_SAFETY_CONTRACT.md.)
- [x] `docs/RT_SAFETY_CONTRACT.md` lives in the repo with the per-PR
      audit checklist. (M1 Session 2 + Session 3 audit rows.)
- [x] Existing 256 ctest cases stay green — 270/270 now.

M2 (Membrane → Mixer rename + SignalType + Channel) is unblocked.

The auto-testing milestone (sections 0-5 below) is **closed and
shipped**. The CI signing handoff (3 of 6 secrets pending) is **still
open** — operator-only Keychain/AppleID work, doesn't gate M1+.
Treat sections 0-5 as historical state.

---

## HISTORICAL — Auto-testing CI signing handoff (still operator-pending, not blocking V7 work)

Auto-testing milestone is shipped on master. CI workflow is in place
but **first run will fail** until the remaining three repo secrets are
added.

**Already done this session (live in repo settings):**

| Secret | Status | Value |
|---|---|---|
| `APPLE_TEAM_ID` | ✓ set | `RR5DY39W4Q` |
| `APPLE_ID` | ✓ set | `itunes@larryseyer.com` |
| `KEYCHAIN_PASSWORD` | ✓ set | random 32-char base64 (throwaway, never leaves CI) |

**Still needed (require operator hands — Keychain GUI + appleid.apple.com):**

1. **`DEVELOPER_ID_CERT_P12_BASE64`** + **`DEVELOPER_ID_CERT_PASSWORD`** —
   export the Developer ID Application cert from Keychain Access:
   - Keychain Access → "login" → My Certificates
   - Find `Developer ID Application: Larry Seyer (RR5DY39W4Q)`. The
     disclosure triangle MUST show a private key under it (else the
     export is useless for signing)
   - Right-click → Export → save as `~/Desktop/sirius-devid.p12`,
     format: Personal Information Exchange (`.p12`)
   - Keychain prompts for an export password — pick anything
     memorable, this becomes `DEVELOPER_ID_CERT_PASSWORD`
   - Keychain may also prompt for your login password to release the
     private key (normal)

2. **`APPLE_APP_PASSWORD`** — app-specific password for notarytool:
   - https://appleid.apple.com → sign in as `itunes@larryseyer.com`
   - Sign-In and Security → App-Specific Passwords → "+"
   - Label `sirius-notarytool` and generate
   - Copy the 19-char password (`xxxx-xxxx-xxxx-xxxx`)

**Two paths to finish the handoff** (operator's choice on return):

- **Path A (operator hands the values to Claude):** drop the .p12 at
  `~/Desktop/sirius-devid.p12`, tell Claude the export password and the
  app-specific password. Claude runs the three `gh secret set` lines.
- **Path B (operator runs the gh commands):**
  ```
  gh secret set DEVELOPER_ID_CERT_P12_BASE64 < <(base64 -i ~/Desktop/sirius-devid.p12)
  gh secret set DEVELOPER_ID_CERT_PASSWORD --body "<the .p12 password>"
  gh secret set APPLE_APP_PASSWORD --body "xxxx-xxxx-xxxx-xxxx"
  ```

**After all six secrets are in place — verification:**

```
gh secret list                    # confirm 6 entries
gh workflow run ci-macos-signed.yml
gh run watch                      # follow the live run
```

Expected: build + sign + verify all green. The "GUI smoke (best-effort)"
step may pass or skip on the GitHub-hosted runner — either is acceptable
(see continue.md §4 for why).

Once the workflow runs green: delete `~/Desktop/sirius-devid.p12`
(security hygiene — the secret is now in GitHub; the local copy is
no longer needed and exposes the private key if leaked). Then the
auto-testing milestone is fully closed out and the next session can
start on the white-paper alignment pass (§6).

---

## 0. Headline

**Auto-testing infrastructure milestone shipped end-to-end** on master
this session. Three commits, all pushed:

| SHA       | Subject                                                                                      |
|-----------|----------------------------------------------------------------------------------------------|
| `4e8a1df` | fix: smoke-persistence.sh — direct binary launch + recursive AX walk + dialog-targeted clicks |
| `f68aa3c` | feat: bash/autotest.sh — local 4-phase macOS verification driver                              |
| (next)    | feat: ci-macos-signed.yml — signed-build + smoke CI workflow + this doc update                |

**Phase A — `bash/smoke-persistence.sh` patched and verified.** Five
AppleScript / Launch Services landmines worked around (catalogued in
§2). Round-trips Save → Load against the signed bundle, exits 0 with
"refs in file: 2" proving v2 shared-encoding ran.

**Phase B — `bash/autotest.sh` runs four phases locally** in ~25s
total on an Apple Silicon dev machine: headless ctest → signed Xcode
bundle build → codesign + spctl verification → GUI smoke. All four
green on master.

**Phase C — `.github/workflows/ci-macos-signed.yml` added** as a
separate workflow from `ci.yml`. Will fire on the next push to master
(this commit). Operator one-time setup: six repo secrets must be
added before the workflow can succeed — see §5.

**Next session = white-paper alignment pass** per the standing
operator sequencing (signing → auto-testing → white-paper alignment).
Auto-testing landed; alignment is up. See §6 for the picked-up
context for that work.

---

## 1. Build + test state

```bash
cd /Users/larryseyer/SiriusLooper
bash bash/autotest.sh        # full 4-phase verification, ~25s
```

Or individual gates:

```bash
# Headless only
cmake --build build --target SiriusTests
ctest --test-dir build --output-on-failure        # 256 / 4283 expected

# Signed bundle only
cmake --build build-xcode --config Release --target SiriusLooper

# GUI smoke against the signed bundle
APP_BUNDLE="build-xcode/app/SiriusLooper_artefacts/Release/Sirius Looper.app" \
  bash bash/smoke-persistence.sh
```

The signed `.app` lives at `build-xcode/app/SiriusLooper_artefacts/Release/Sirius Looper.app`.
Developer ID Application: Larry Seyer (RR5DY39W4Q), hardened runtime,
audio-input entitlement, spctl-accepted.

The dev-loop `build/.app` is unchanged — ad-hoc-signed, fast iteration.

---

## 2. AppleScript / macOS landmines documented this session (for future GUI smoke work)

Each of these bit hard during Phase A. Recorded here so the next time
someone touches `bash/smoke-persistence.sh` or writes a new
osascript-based GUI driver, they don't have to rediscover them.

1. **`open ...app` returns -10825 on dev-tree paths.** Launch Services
   refuses to launch the Developer-ID-signed bundle from
   `build-xcode/...` even though `spctl` accepts it. The ad-hoc
   sibling at `build/...` opens fine. Suspected cause: the ad-hoc
   bundle's invalid `Identifier=Sirius Looper` (string with space)
   collides in LS with the signed bundle's valid
   `com.larryseyer.siriuslooper`. Workaround: launch the binary
   directly via `${APP_BUNDLE}/Contents/MacOS/${APP_NAME}` —
   bypasses LS entirely, more reliable for automation anyway.
   Root cause filed as its own todo entry.

2. **`entire contents of window 1` strips role info.** Iterating
   returns elements where `role of elt` doesn't resolve cleanly; the
   `AXButton` role check matched zero elements even when buttons were
   present. Workaround: explicit two-level recursion using
   `every UI element of window 1` then `every button of grp`.

3. **Tab buttons are nested one AXGroup deep.** JUCE's
   `TabbedComponent` exposes tab buttons as AXButton children of an
   AXGroup, not at the window's top level. Save/Load on the
   Preparation pane are similarly nested. Recursion in #2 catches
   both.

4. **`keystroke "g" using {command down, shift down}` silently no-ops
   inside NSSavePanel/NSOpenPanel on Sequoia** when the filename field
   has focus. The text field appears to eat the modified keystroke.
   Workaround: `key code 5 using {command down, shift down}` (5 =
   physical 'g' keycode) fires the system shortcut reliably.

5. **NSSavePanel/NSOpenPanel open as separate top-level windows,
   not sheets.** `sheets of window 1` returns 0 after clicking
   Save/Load. The dialog is a sibling window whose name starts with
   the title prefix ("Save Sirius session..." / "Load Sirius
   session..."). Target it by `first window whose name starts with
   "..."` and click the action button by name. Bonus: NSOpenPanel's
   "Open" button stays *disabled* until a file is selected in the
   listing — Cmd+Shift+G navigates to the directory but doesn't
   select, so the smoke script falls back to Return when the action
   button is disabled.

6. **Activate-and-keystroke must be in the same osascript block.**
   When `tell application "X" to activate` and `keystroke ...` are
   issued from separate osascript invocations, focus can slip back
   to the terminal between calls and the keystrokes land in the
   wrong app. Solution: combine them in one heredoc.

---

## 3. The three small follow-ups surfaced this session

All filed in `todo.md` near the top. None block anything; all are
"while we're in this neighborhood" cleanups.

1. **`com.apple.security.get-task-allow=true` in signed entitlements.**
   Visible in `codesign -dv --entitlements -` output. Fine for non-
   notarized dev builds; rejected by Apple's notarization service.
   Must be `false` (or absent) before any notarized release. Likely a
   Xcode default the CMake block needs to override.

2. **`open` returns -10825 on the signed dev-tree bundle.** Root cause
   of landmine #1 above. Workaround in place in the smoke script;
   investigation deferred. End-user launches against a notarized
   bundle in /Applications are not affected.

3. **Ad-hoc `build/` bundle has invalid `Identifier=Sirius Looper`.**
   Should be `com.larryseyer.siriuslooper` to match the Xcode-gen
   build. The mismatch is the leading suspect for #2's LS confusion.
   Fix the CMake config that sets the bundle ID on the Unix Makefiles
   target.

---

## 4. CI status

`ci.yml` (the original push/PR matrix):
- macOS: green (headless tests).
- Linux + Windows: red on a pre-existing int64→juce::var ambiguity in
  `persistence/src/SessionFormat.cpp`. **Explicitly deferred** under
  the `feedback-mac-first-linux-windows-last` rule — not a candidate
  for "what's next."

`ci-macos-signed.yml` (added this session):
- Will fire on the next push to master. **First run will fail**
  because the six required secrets are not yet in the repo. Operator
  step: add them per §5, then push (or `workflow_dispatch`) to verify.
- GUI smoke step is `continue-on-error: true` from day one — GitHub-
  hosted macOS runners can't grant the runner user Accessibility
  permission without SIP-disabling tricks that hosted runners don't
  support. Build + sign + codesign-verify is the hard gate; smoke is
  best-effort signal.

---

## 5. Operator one-time setup before CI signing works

In GitHub repo Settings → Secrets and variables → Actions, add:

| Secret name                    | Value source                                                                                                |
|--------------------------------|-------------------------------------------------------------------------------------------------------------|
| `DEVELOPER_ID_CERT_P12_BASE64` | Export `Developer ID Application: Larry Seyer (RR5DY39W4Q)` from Keychain Access as `.p12` with a password, then `base64 -i Developer-ID.p12 \| pbcopy` |
| `DEVELOPER_ID_CERT_PASSWORD`   | The `.p12` export password just set                                                                         |
| `APPLE_ID`                     | `itunes@larryseyer.com` (per `reference-apple-id` memory)                                                   |
| `APPLE_APP_PASSWORD`           | App-specific password from appleid.apple.com (NOT the Apple ID password — generate one specifically for notarytool) |
| `APPLE_TEAM_ID`                | `RR5DY39W4Q`                                                                                                |
| `KEYCHAIN_PASSWORD`            | Any opaque string the operator picks — used as the throwaway CI keychain password                            |

`APPLE_ID` / `APPLE_APP_PASSWORD` / `APPLE_TEAM_ID` aren't strictly
needed for the current workflow (no notarization step yet), but
they're listed here so they get added once and are ready when
notarization moves into a release-tag workflow.

After secrets land: trigger the workflow via `workflow_dispatch` in
the Actions tab (or just push), and verify the run goes green
("GUI smoke: PASS" or "GUI smoke: SKIPPED/FAILED on CI runner" —
either is acceptable; build + sign + verify must pass).

---

## 6. Next milestone — white-paper alignment pass — SUPERSEDED-AND-SPEC'D 2026-05-17

The alignment pass that this section queued has been spec'd in full at
`docs/superpowers/plans/2026-05-17-v7-alignment.md` (24 milestones across
11 parts; M1 = Audio I/O foundation + RT-safety contract audit; see
RESUME HERE at the top of this file). The original V2 white paper
referenced below is superseded by V7 (`docs/Sirius_Looper.md`); the
V2→V7 transition guide (`docs/sirius-looper-v2-to-v7-transition.md`)
is the bridge.

Original §6 contents preserved for historical context follow; treat as
read-only:

> Per the operator's stated sequence (signing → auto-testing → white-
> paper alignment). Auto-testing is done; alignment is up.
>
> **Reading order before starting alignment work:**
>
> 1. `docs/Sirius Looper Whitepaper V2.md` — the white paper.
>    *(Superseded — read V7 at `docs/Sirius_Looper.md` instead.)*
> 2. `docs/Sirius Looper User Guide.md` — the operator-facing how-to.
> 3. `docs/superpowers/specs/2026-05-16-shared-placement-design.md` — the
>    most-recent shipped spec.
> 4. `todo.md` — alignment-adjacent entries.
>
> **What this session does NOT decide:** the scope, structure, or
> deliverables of the alignment pass. That's the next session's first-
> half work (brainstorm + spec) before any code lands.
>    *(That brainstorm + spec is what 2026-05-17 produced. The plan
>    file is the output.)*

---

## 7. Authoritative references

- `~/.claude/CLAUDE.md` — global rules (auto-loaded).
- `~/.claude/projects/-Users-larryseyer-Sirius-Looper/memory/MEMORY.md`
  — auto-memory index (auto-loaded). Two new entries this session:
  - `feedback-mac-first-linux-windows-last` (Platform order:
    **macOS → iOS → Windows → Linux**; iOS = AUv3 only)
  - `feedback-esc-while-typing-is-not-abort` (silent tool rejections
    usually mean the operator is mid-message)
- `todo.md` — deferred items register. Auto-testing milestone marked
  SUPERSEDED-AND-IMPLEMENTED at the top; three new follow-ups filed
  beneath it; legacy GUI-smoke and Load-dialog entries updated to
  reflect resolution.
- `/Users/larryseyer/.claude/plans/read-continue-and-proceed-floating-pelican.md`
  — the auto-testing plan that just shipped. Self-contained, can be
  archived or kept as a structural template.
- `bash/autotest.sh` — single command for full local verification.
- `bash/smoke-persistence.sh` — GUI smoke driver (operator must have
  Accessibility access granted to the shell that runs it).
- `.github/workflows/ci-macos-signed.yml` — signed CI workflow (six
  secrets required, see §5).
- `docs/Sirius Looper Whitepaper V2.md` and
  `docs/Sirius Looper User Guide.md` — next milestone's primary
  source material.

---

## 8. One-paragraph orientation if everything else has changed

Auto-testing infrastructure is in place. `bash/autotest.sh` is the
local one-shot gate; `ci-macos-signed.yml` is the per-push CI gate
(needs operator-added secrets to actually run, see §5). Next
milestone is the white-paper alignment pass — that's a design
session, not a code session, so start with brainstorm + spec before
touching `app/` or `ui/`. Linux/Windows remain explicitly deferred.
