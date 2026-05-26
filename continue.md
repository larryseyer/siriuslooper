# Session Continuation — file-input AUDIO ROUTING slice CLOSED; 4 follow-ons queued in todo.md

## ▶ 0. Read these first (60 seconds)

1. **The file-input audio-routing slice is done.** Press ▶ on a file-input
   player window → audio reaches the speakers. Operator-verified on
   `ebab565` (2026-05-26): "Tape play works." The predecessor file-input
   slice (UI + registry + worker, closed at `aa67fcd`) had the engine
   wiring as its honestly-deferred follow-on; this slice closes it.
2. **Baseline.** `master` at `ebab565`, local == origin (confirm with
   `git log -1 --oneline` and `git status --short`). Pre-existing
   `IDA_Naming_Decision.md` rename is unrelated, leave it.
3. **Test surface.** `ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" -j` → **773 / 773** (baseline 762 + 11 new from Tasks 2-5 of this slice: 3 in `FileInputSourceTests`, 1 in `FileInputRegistryTests`, 7 in the new `InputMixerFileInputTests`). Allow 3 transient flakes (#279 OOP-host / #350 stateBlobForSlot / #756 Bus-process-OOP) — all pass on `--rerun-failed`. `./build/tests/IdaTests "[file-input]"` → 35 cases / ~2800 assertions.
4. **Four follow-ons surfaced during operator eyes-on (2026-05-26).** All
   four are in `todo.md` with full design notes; operator's direction:
   "spec them." Order of complexity (smallest → largest):
   - (A) Player window: frameless + semi-transparent + always-on-top.
   - (B) Transport sync (LMC start/stop/ignore).
   - (C) File-input MIDI source.
   - (D) File-input Video source.

---

## ▶ 1. What landed (audio-routing slice — Tasks 1–6)

Spec + plan (`e313e34` + `4c06d86`) preceded the slice. 10 commits, chronological:

| Commit | Task | Subject |
|---|---|---|
| `602f035` | T1 | core — IFileInputSourceRegistry interface + kFileInputIdBase constant |
| `c5c7507` | T1↻ | core — kFileInputIdBase uses inline constexpr (avoid per-TU internal linkage) |
| `b4f9343` | T2 | audio — FileInputSource raw-pointer pullInto overload + pullIntoStatic thunk |
| `f366a0a` | T3 | audio — FileInputRegistry implements IFileInputSourceRegistry |
| `d9f0922` | T4 | engine — InputMixer file-input dispatch via cached FileInputPullCallable |
| `9b5b179` | T4↻ | engine — InputMixer file-input loop runs when deviceIn is null + clarify RT-safety contract comment |
| `ffc1412` | T5 | test — InputMixer file-input dispatch edge cases (no-registry, unknown id, mixed, multi-call, false-return) |
| `c68210e` | T5↻ | test — case 6 also verifies recovery after pull returns false (per spec) |
| `4b75a0f` | T5↻ | test — add postStripPointer null guards to file-input unknown-id case |
| `ebab565` | T6 | app — wire FileInputRegistry into InputMixer; file-input audio now reaches speakers |

(↻ = code-quality-review follow-on. Every task ran through implementer → spec review → code-quality review per `superpowers:subagent-driven-development`.)

**Where the engine pull happens:** `engine/src/InputMixer.cpp` `renderInputGraph` has a parallel loop over `channelFilePulls_` (a map of `ChannelId → FileInputPullCallable`) that invokes the cached function pointer, silence-fills on false return, then runs strip + routing exactly like the device-input loop. The device-input loop is now wrapped in `if (deviceIn != nullptr && numDeviceChannels > 0)` so the file-input loop survives the headless / no-device scenario.

**Where the JUCE-free seam lives:** `engine/include/ida/IFileInputSourceRegistry.h` defines `FileInputPullCallable { fn, userdata }` (POD; `noexcept` function pointer) + `IFileInputSourceRegistry` (1 pure-virtual). `FileInputRegistry` (audio/) implements it; resolution happens on the message thread at `setChannelFileInputSource` time and caches the callable. Audio thread never touches the registry.

**Where the app wiring lives:** `app/MainComponent.cpp` MainComponent ctor calls `inputMixer_->setFileInputSourceRegistry(&fileInputRegistry_)` once (alongside the other one-shot setters). `rebuildInputStrips()` calls `inputMixer_->setChannelFileInputSource(channelId, inputId)` for file-input strips (vs. `setChannelInputSource` for hardware). Both setters run inside the existing `removeAudioCallback / addAudioCallback` bracket that `rebuildInputStrips()` already provides.

**Why no extra bracket on `registerFileInput`:** `FileInputRegistry::sources_` is `unordered_map<int64_t, unique_ptr<FileInputSource>>`. Rehash moves the unique_ptr but NOT the pointee. The audio thread's cached `FileInputSource*` (via channelFilePulls_'s userdata) remains valid across registry rehashes. The cached-callable installation happens via `rebuildInputStrips()` which already brackets the audio callback.

---

## ▶ 2. The 4 follow-ons surfaced 2026-05-26 (queued in todo.md)

Operator played a file → audio works → identified 4 gaps:

### (A) Player window: frameless + semi-transparent + always-on-top — **smallest, recommended next**
- Remove macOS title bar / traffic-light controls; show as a floating semi-transparent IDA source surface, not a stock OS window.
- Add an "always on top" toggle (so the player doesn't get lost behind other windows).
- 4 open design questions (close gesture, drag-to-move, transparency level, cross-platform parity).
- Likely 2 small commits.

### (B) Transport sync (LMC start/stop/ignore) — medium
- File-input playback can sync to IDA's transport (LMC). Operator said "start/stop/ignore" — 3 modes.
- Per-file-input `TransportSyncMode` field; persists in FileInputDescriptor.
- 6 open design questions (mode enum shape, default, pause-vs-rewind on stop, playlist interaction, persistence, UI).

### (C) File-input MIDI source — medium-large
- Whitepaper §6.6: file inputs are playlists of audio AND MIDI. Current slice ships only audio.
- New `FileMidiInputSource` parallel to `FileInputSource`; reads .mid; drives a MIDI event ring; binds to a `SignalType::Midi` channel.
- 4 open design questions (polymorphism vs parallel registry, routing destination, MIDI-clock-vs-LMC, playlist semantics).

### (D) File-input Video source — largest
- Whitepaper §6.6 (implied): video as a file input. Adds a display surface + frame timing + audio sub-track.
- New `FileVideoInputSource` + a video display surface (probably a floating window like the audio player).
- 5 open design questions (display surface model, audio sub-track path, A/V sync clock, codec support, RT discipline).

Full design notes in `todo.md` (top 4 entries dated 2026-05-26). Each entry lists files, open design questions, and "what's needed to finish" so the brainstorm session starts with full context.

---

## ▶ 3. Architectural deviations from the audio-routing plan (carry forward)

### Deviation A — `IFileInputSourceRegistry.h` lives in `engine/`, not `core/`

**Why:** the spec said "create `core/include/ida/IFileInputSourceRegistry.h`" but `Channel.h` (which defines `InputId`, required by the interface) actually lives in `engine/include/ida/`. Placing the new header in `core/` would force `core` to depend on `engine`, inverting the layer order. The interface stays JUCE-free (only includes `Channel.h`); `audio/` PUBLIC-links `Ida::Engine` already, so consuming the engine-located interface is the existing dependency direction. Code reviewer initially flagged this as Critical; verified by inspection that the reviewer's premise (audio doesn't link engine) was wrong — audio's `FileInputRegistry.h` has been including engine's `Channel.h` since the predecessor slice.

A future cleanup could move BOTH `Channel.h` and the interface to `core/` (an existing TODO comment on `InputId` already anticipates this). Out of scope for this slice; flagged for a future "promote core types" pass.

### Deviation B — Parallel `channelFilePulls_` map (not embedded callable on channel state struct)

**Why:** the spec said "add a `FileInputPullCallable` field to the channel-state struct." The implementer chose a parallel `std::unordered_map<ChannelId, FileInputPullCallable> channelFilePulls_` keyed by ChannelId, sibling to the existing `channelSources_` and `channelPreFaderSends_` maps. Stated rationale: matches the existing pattern; keeps the device-input gather loop byte-identical; disjoint maps process disjoint channels.

`removeChannel` erases from `channelFilePulls_` alongside the other sibling maps — verified for correctness during spec review.

### Deviation C — Separate parallel loop in `renderInputGraph` (not `if/else` branch)

**Why:** the spec said "branch on `channel.filePull.valid()`" inside the existing per-channel gather loop. The implementer chose two disjoint loops — first the device-input loop over `channelSources_`, then the file-input loop over `channelFilePulls_`. Stated rationale: a channel can be in only one map at a time (the setters are mutually exclusive by call-site contract), so the two loops process disjoint sets — no double-processing risk. Strip + routing + sends tail is byte-for-byte identical between the two loops.

The downside is that any future change to the strip+routing tail must be applied to both loops symmetrically. Documented in the source.

### Deviation D — Persistence not touched

**Why:** `channelFilePulls_` does NOT go through `exportGraphState`/`importGraphState`. The persisted graph carries the channel's InputId; on session load, `MainComponent::rebuildInputStrips()` re-binds the cached callable by calling `inputMixer_->setChannelFileInputSource(channelId, inputId)`. The pull callable is derived state, not authoritative state.

### Deviation E — Test pan-law adjustment

**Why:** the plan's primary test asserted exact values `0.25f` / `-0.75f` at the post-strip pointer. `ChannelStrip<Audio>::process` applies equal-power center-pan (cos(π/4) ≈ 0.70710677), so post-strip values are scaled by that gain. All 7 file-input edge-case tests use a `constexpr float kPanGain = 0.70710677f` constant with `.margin(1e-5f)` on the scaled comparisons. The constant could be hoisted to file scope in a future cleanup (current pattern is per-case).

---

## ▶ 4. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| HEAD | `ebab565` (verify with `git log -1 --oneline`) |
| `git status --short` | only the pre-existing `IDA_Naming_Decision.md` rename — unrelated |
| `ctest --test-dir build -E "(PluginEditor\|MainComponentPlug)" -j` | **773 / 773** (baseline 762 + 11 from this slice; 3 transient flakes pass on `--rerun-failed`) |
| `./build/tests/IdaTests "[file-input]"` | 35 cases / ~2800 assertions all pass |
| Operator eyes-on | **Confirmed 2026-05-26: "Tape play works."** Audio reaches speakers. |

---

## ▶ 5. Resume protocol for next chat

1. Read this file (you're doing it).
2. Pick one of the 4 follow-ons in §2 to brainstorm/spec/implement. **Recommended order:**
   - **(A) Player window polish** first — smallest scope, operator already articulated the constraints, lowest risk. Lands a clean UX win quickly.
   - **(B) Transport sync** second — closes a real functional gap and unlocks (C)/(D) later (since MIDI + video both want transport sync from day one).
   - **(C) MIDI** third — natural sister to audio; reuses most of the FileInputSource architecture; SignalType::Midi already exists.
   - **(D) Video** last — biggest scope; new display surface; new media pipeline.
3. For whichever follow-on the operator picks: invoke `superpowers:brainstorming` against the relevant `todo.md` entry. The entries list the open design questions and the anticipated file surface — brainstorm starts with full context, not from zero.
4. After brainstorm → spec → plan → subagent-driven implementation, per the file-input slice precedent.

Reference docs for any of the 4 follow-ons:
- Audio file-input architecture: `docs/superpowers/specs/2026-05-25-file-input-design.md` + `docs/superpowers/specs/2026-05-26-file-input-audio-routing-design.md`
- Operator UI: `app/FileInputPlayerWindow.{h,cpp}` (audio player window — model for MIDI/video variants)
- Engine seam: `engine/include/ida/IFileInputSourceRegistry.h` (1-method JUCE-free interface — MIDI/video can reuse the same pattern with their own sister interfaces)
- LMC (for transport sync): `engine/include/ida/Lmc.h`
- Whitepaper: `docs/IDA_Whitepaper_V9.md` §6.6 (file inputs are playlists), §7.2 (the playlist scope semantics)

---

*End of slice. Audio file-input plays end-to-end. Four follow-ons queued; operator picks order next session.*
