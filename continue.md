# Session Continuation — input-bus MON slice: 10/14 tasks landed; resume at T11

## ▶ 0. Read these first (2 minutes)

1. **Whitepaper amended + design + plan landed** (`8d34fc0` whitepaper §6.6/§7.2/Glossary; `9bd87ea` plan). Per-input-side-node MON is now spec — input channels, aux buses, AND FX returns each carry the toggle.
2. **10 of 14 tasks shipped, all reviewed (spec + code-quality), all green.** Engine + persistence + initial UI wiring done. T11-T14 remain — they're the rest of the UI plumbing + the operator-verify gate.
3. **Next chat starts with T11.** Read the design at `docs/superpowers/specs/2026-05-25-input-bus-mon-design.md` and the plan at `docs/superpowers/plans/2026-05-25-input-bus-mon.md` (§ Task 11 onward). The Task tracker has the 14 entries with current status — T1-T10 completed, T11-T14 pending.

---

## ▶ 1. WHAT'S NEXT — resume at T11

### T11 — `refreshOutputMixer{,MonChannels,Destinations}` walk bus MONs

The plan task is the **most complex remaining slice** in terms of file touch points: one new private vector on MainComponent, three function bodies modified to handle the combined channel-sourced + bus-sourced MON-band rows. Full step-by-step is in `docs/superpowers/plans/2026-05-25-input-bus-mon.md` Task 11 — verbatim code blocks provided.

Shape:
1. Add `std::vector<ida::BusId> monStripInputBusIds_` to MainComponent's private section, parallel to `monStripInputChannelIds_`. Sentinel `BusId{0}` marks channel-sourced rows (InputMixer's first bus id is 1, so 0 is safe).
2. Rewrite `refreshOutputMixerMonChannels` to combine channel-sourced + bus-sourced strips into one MON band (bus-sourced labelled by bus name, not "MON N").
3. Extend `refreshOutputDestinations` MON block (landed in commit `2da5459`) to resolve each row's OutputChannelId via `busMonitorOutputChannel` when bus-sourced, else `channelMonitorOutputChannel`.
4. Extend `refreshOutputMixer`'s MON-meter loop with the same bus-vs-channel switch.

After T11 lands, the Output Mixer pane will actually SHOW a bus MON strip when the operator toggles MON on a bus (T9 + T10 created the engine channel but it has no UI strip yet — T11 closes the visual gap).

### T12 — Load-handler replay block for bus MON

Mirror of the channel-MON replay at `app/MainComponent.cpp:7106-7111`. After `inputMixer_->importGraphState`, iterate `loadedInputMixer->buses` and call `setBusMonitorMode(b.busId, On)` for any MON-on bus. The engine's `importGraphState` already replays the mode (T6), but the OutputMixer isn't attached during the engine-import call, so this caller-side replay engages the route after attachment.

### T13 — `refreshInputMixer` + `rebuildBusStrips` push bus MON button states

Mirror of the channel-side `setMonitorModes` call. After load (or any structural bus rebuild), the InputMixerPane's bus MON button labels must sync from engine state via `inputMixerPane_->setBusMonitorModes(busModes)`.

### T14 — Clean rebuild + operator-verify recipe

`rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IDA -j`. Final ctest pass. Then refresh `continue.md` with the 7-step operator verification recipe (defined in the plan's T14).

---

## ▶ 2. What landed this chat (11 commits, oldest first)

| SHA | Subject |
|---|---|
| `b911f03` | feat: MixerBusState carries monitorMode (V9 §7.2 per-input-node MON) with default-Off + equality |
| `2cd00d9` | docs: MixerBusState.monitorMode — note absence of monitorOutputPair sibling |
| `8c8c99a` | feat: Bus::postProcessingPointer — stable L/R accessor on processedBuffer_ |
| `b13577a` | docs: Bus::postProcessingPointer — spell out the buses_.reserve-based stability invariant |
| `6196cd2` | feat: InputMixer::setBusMonitorMode(On) mints OutputMixer channel from Bus::postProcessingPointer |
| `611a437` | test: InputMixer::setBusMonitorMode — Off teardown, idempotence, unknown-id, attach-deferral |
| `7bdabca` | feat: InputMixer::exportGraphState captures bus monitorMode |
| `86a0ad8` | feat: InputMixer::importGraphState replays bus monitorMode (mirror of channel-side replay) |
| `a237339` | feat: SessionFormat — MixerBusState.monitorMode JSON serialization, default-suppress |
| `afaccbe` | test: InputMixer bus MON — full JSON round-trip E2E sanity |
| `ef066fd` | feat: InputMixerPane — bus strip MON button (label/tooltip mirror of per-channel MON) |
| `f7ad3b5` | feat: MainComponent — onBusMonitorModeChanged relays to InputMixer::setBusMonitorMode |

Plus earlier this session (bug B + design):
| SHA | Subject |
|---|---|
| `2da5459` | fix: MON output strips — wire setMonDestinations into refreshOutputDestinations + JSON round-trip test |
| `8d34fc0` | docs: whitepaper §6.6/§7.2/Glossary — MON generalized to per-input-side-node |
| `9bd87ea` | docs: input-bus-MON implementation plan |

Branch tip is **`f7ad3b5`** on `master` (local == origin) before this handoff refresh; the handoff refresh you're reading lands as the next commit.

---

## ▶ 3. Baseline as of `f7ad3b5`

| Check | Result |
|---|---|
| Branch | `master` |
| `ctest -E "(PluginEditor|MainComponentPlug)"` | (run before T14) baseline was 715 before slice; new tests added across T1-T8 |
| `[input-mixer][bus-monitor]` | 7 cases, all green (1 from T3 + 4 from T4 + 1 from T5 + 1 from T6) |
| `[input-mixer][bus-monitor][persistence][json]` | 1 case, 4 assertions, green |
| `[sessionformat][bus-monitor]` | 1 case, green |
| `[bus][monitor-tap]` | 1 case, 8 assertions, green |
| `[mixer-graph-state][bus-monitor]` | 1 case, 4 assertions, green |
| Build target `IDA` | clean (T9 + T10 compile-only checks passed) |
| Operator GUI verify | **not yet done — T14 gate** |

---

## ▶ 4. How the slice composes (10 of 14 tasks)

- **T1 — `MixerBusState::monitorMode`** field added with default Off, operator== extended.
- **T2 — `Bus::postProcessingPointer(side)`** const accessor exposes `processedBuffer_` for L/R sides; stable pointer because `InputMixer::buses_` is `reserve()`d to `kMaxInputBuses` up-front and never erases.
- **T3 — `InputMixer::setBusMonitorMode(On)`** mints an OutputMixer channel, installs a `ChannelStrip<Audio>`, wires `setChannelAudioSource(bus->postProcessingPointer(0/1))`. Mirror of `setChannelMonitorMode` line-for-line.
- **T4 — Branch tests** lock Off teardown / idempotence / unknown-id / attach-deferral behaviors.
- **T5 — `exportGraphState`** captures `entry.monitorMode = busMonitorMode(bus.id())`.
- **T6 — `importGraphState`** replays via `setBusMonitorMode` after buses minted, before main-outs applied.
- **T7 — JSON serialization** default-suppress in `busStateToVar` / `busStateFromVar`. (Helpers `monitorModeToken`/`monitorModeFromString` hoisted above the bus serializer — clean cut-and-paste, no duplicates.)
- **T8 — E2E JSON test** proves the full chain (export → JSON → deserialize → import) round-trips cleanly. **This is the "engine + persistence half is done" gate** — passed.
- **T9 — InputMixerPane bus strip MON button** with state vectors + helpers + resized() layout in `monitorRow` band, mirroring channel-row order. Stale "Buses have no Monitor button" comment cleaned up.
- **T10 — `onBusMonitorModeChanged` relay** in MainComponent → `setBusMonitorMode` (bracketed audio callback) → `refreshOutputMixerMonChannels`.

After T9 + T10: the bus-row MON button is wired to the engine. The engine creates the OutputMixer channel. **But the OutputMixer pane doesn't show a strip for it yet — that's what T11 fixes.**

---

## ▶ 5. Out of scope (queued; demoted below §1)

1. **Phrase strip meter is dead** (carried over — see prior handoffs).
2. **`MainComponentPluginEditorTests` SIGTERMs** — pre-existing, five+ continue.md's now.
3. **`InputMixerMonitorMuteLeakTests.cpp` references the deleted `DirectLayer` header.** Compile-fails; build skips.
4. **TAPECOLOR OTTO inbox** — `[FROM OTTO → IDA]` `needs-ack` entries. Operator standing direction: defer while OTTO is debugging.

---

## ▶ 6. House rules respected

- ✅ Worked on `master`, no feature branch.
- ✅ Commit + push to `origin/master` after every task / follow-on.
- ✅ Single-line commit titles.
- ✅ Subagent-driven implementation with spec + code-quality review per task. Follow-on commits used for the few MINOR fixes (T1 + T2 had one each); no `--amend` anywhere.
- ✅ Whitepaper amendments landed FIRST (`8d34fc0`), THEN design (`9bd87ea` plan), THEN code — no architectural surprises buried in implementation commits.
- ✅ Each subagent committed + pushed its own task. Controller (this Claude) only landed two small follow-on doc-comment commits in response to code-quality MINOR feedback.

---

## ▶ 7. Resume protocol for next chat

1. Read **this file** (continue.md).
2. Read the **plan** at `docs/superpowers/plans/2026-05-25-input-bus-mon.md` — jump to "Task 11" and follow the verbatim steps.
3. Use the **subagent-driven flow** (already-loaded skill `superpowers:subagent-driven-development`). Dispatch implementer → spec reviewer → code-quality reviewer per task. The four remaining tasks are well-scoped:
   - T11 — heaviest of the four; touches 4 function bodies in MainComponent.cpp; adds one private vector.
   - T12 — 5-line load-handler replay block.
   - T13 — two short pushes in `refreshInputMixer` + `rebuildBusStrips`.
   - T14 — clean rebuild + final ctest + operator-verify recipe written into continue.md.
4. After T14 lands, the **operator does eyes-on verification** per the 7-step recipe T14 writes.

The Task tracker in this session has tasks 1-10 marked completed and 11-14 pending — those will need to be re-created in the new session since tracker state doesn't survive `/clear`.

---

*End of input-bus-MON 10/14 handoff. Next chat: T11 → T12 → T13 → T14 → operator verify.*
