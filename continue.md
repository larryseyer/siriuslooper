# Session Continuation — 2026-05-30 (T0b playback-resolution path COMPLETE)

## Current State

**Tier-0 / Phase T0b — the playback-resolution path — is DONE.** A recorded
phrase now streams from the on-disk tape store into its Output-Mixer channel,
driven by OTTO's transport playhead. Built subagent-driven against the committed
plan (`docs/superpowers/plans/2026-05-30-playback-resolution-path-t0b.md`), each
task with spec + code-quality review + fix loop, plus a final whole-subsystem
holistic review (verdict: ship-ready for operator ear-test, blocking item fixed).

The **operator ear-test is the remaining confirmation** — the engine path is
fully headless-proven; runtime/GUI is operator-verified (the agent cannot hear it).

- **Plan (committed):** `docs/superpowers/plans/2026-05-30-playback-resolution-path-t0b.md`.
- **Design spec:** `docs/superpowers/specs/2026-05-30-render-path-and-tape-store-design.md`
  (Phase T0b, §155–229; the "why" — whitepaper §3.6/§5.2/§5.7/§6.6/§8).

## Repo State (verified, not narrated)

- **IDA** master HEAD = **`7ac220e`**, **origin/master in sync** (all pushed).
  Clean tree except `external/sfizz` (pre-existing untracked — leave it).
- **OTTO** submodule UNCHANGED this session. No `external/OTTO/` edits, no SHA bump.
  Inbox: `[FROM OTTO → IDA]` empty; the `[FROM IDA → OTTO]` entries are `needs-ack`
  addressed to OTTO's Claude — not ours to prune.
- **Final verification at HEAD:** CLEAN build (`rm -rf build` + reconfigure) of
  `IdaTests` AND the signed `IDA.app` both succeed; full `ctest` = **894/895** — the
  single non-pass is the documented separately-run `MainComponentPluginEditorTests_NOT_BUILT`
  (established baseline, NOT a regression). `[tape-playback]` = **21 cases / 790
  assertions**, all green.

## What T0b delivers (on disk + tested)

Data flow (input mixer → tape → **[T0b]** → output mixer):
- **Playhead** (`engine/.../TransportPlayhead.h`, `otto-bridge/.../OttoHost.*`):
  `OttoHost::renderBlock` publishes `TransportPlayhead{positionInSeconds,isPlaying}`
  via lock-free atomics. v1 identity calibration: positionInSeconds =
  elapsed-PLAYING-samples / sr (monotonic-while-playing by construction; sidesteps
  OTTO's pattern-null position quirk). `prepare()` resets the published atomics too.
- **Snapshot** (`engine/.../ActiveReadsSnapshot.h`): `PhraseSlotRead{slot,
  tapeSampleStart,active}` + fixed-cap `ActiveReadsSnapshot` (kMaxPhraseSlots=64) +
  `ActiveReadsPublisher` (SPSC seqlock; lock-free bounded-retry `read` on the audio
  thread, never wait-free — comment says so).
- **Resolver** (`engine/.../PlaybackResolver.*`): control-worker (~10ms) reads the
  playhead → exact `Rational(llround(sec*sr), llround(sr))` LMC time →
  `RenderPipeline::activeReadsAt` (tree-walk, off-thread) → maps ConstituentId→slot
  → steers each slot's prefetcher (`setTargetSample`) → publishes the
  pre-resolved snapshot. Injected std::function collaborators (testable in isolation).
- **Prefetcher** (`audio/.../TapePrefetcher.*`): per phrase channel; worker decodes
  tape records AHEAD into a lock-free stereo PCM ring; audio-thread `pull` is
  wait-free + zero-fills underrun. **Locates records via the reader index's `lmcTs`
  timestamps** (NOT fixed framesPerRecord) — correct for VARIABLE-size live records
  (the writer frames one audio block per record; block size is not guaranteed
  constant). `open(file, registry, double sampleRate, loopLengthSamples)`.
- **Reader** (`audio/.../TapeRecordReader.*`): T0a + Task-1 change — caches one read
  stream across `readAudioRecord`, reset on `refresh` (single-worker contract documented).
- **Phrase scratch** (`engine/.../OutputMixer.*`): stable per-channel stereo scratch
  (mirror of `InputMixer::postStrip_`) — `ensurePhraseScratch`/`phraseScratchPointer`/
  `mutablePhraseScratch`; erased on `removeChannel` (no stale-on-id-reuse). The
  channel's audio source points at it via `setChannelAudioSource`.
- **Playback step** (`audio/.../AudioCallback.*`): new RT-safe step between OTTO
  render and OutputMixer dispatch — seqlock read + per-slot `pull` → memcpy into
  phrase scratch (clamped to `Bus::kMaxBusMixSamples`); active→inactive zeroed once.
  Zero alloc/lock/IO/decode/tree-walk on the audio thread (alloc test enforces it).
- **Wiring** (`app/MainComponent.*`): owns the registry (PCM+FLAC) / publisher /
  resolver / `RenderPipeline` (from `demo_.root` + `demo_.sessionToLmc`) / per-channel
  prefetchers / slot map. Wired in `refreshOutputMixerPhraseChannels` (resolver
  stopped + audio callback detached around the whole mutation — provably quiescent)
  + the device-prepare path + teardown (reverse order). Initial session load routes
  through the same `refreshOutputMixerPhraseChannels` path.

## T0b commit trail (this session, in order — all pushed, HEAD `7ac220e`)

`f2fa903` plan → `47ba93d`+`7b3ed5e` (T1 reader cache) → `5613aca`+`915a9f7` (T2
playhead) → `406b949`+`13cea01` (T3 snapshot) → `5ab2739` (T4 phrase scratch) →
`f38a1a9`+`bc96fbf` (T5 prefetcher) → `57f6d3d`+`d37075b` (T6 resolver) →
`27b2981`+`630ff51` (T7 playback step) → `8a3990b` (T8a e2e) →
`7d18842`+`78aa904` (T5b index-by-lmcTs, variable-block correctness) →
`faf353d`+`7302a14` (T8b MainComponent wiring) → `7ac220e` (holistic-review fix:
scratch clamp + seqlock-race note).

## Operator ear-test (the remaining confirmation)

The signed `IDA.app` is freshly clean-built. To eyes/ears-on:
1. Launch `IDA` (Desktop alias) — or the agent can launch it.
2. Record input to a tape so a phrase pill exists (its tape `.idatape` is on disk).
3. Hit play (OTTO transport). The phrase should sound through its Output-Mixer
   strip and its per-phrase meter should move; stop → silence.
- Caveat: a phrase only sounds if its tape FILE already exists (recorded). A pill
  whose tape isn't recorded yet stays silent until the next channel refresh re-opens
  it (no auto-retry on tape-create — see v1 limits).

## Honest v1 limits (in `todo.md`, NOT blockers)

- **FreeRunning leaf loops only** (RenderPipeline M3 reality).
- **Identity device calibration** (positionInSeconds→sample = round(sec*sr));
  real loopback calibration deferred.
- **Playhead = elapsed-playing-seconds**, not OTTO's ppq position; transport
  relocate/seek within OTTO's timeline not yet reflected. Sub-block play-start
  error (whole first block credited) — RD1 stub.
- **Prefetcher wrap is from 0** (`s %= loopLengthSamples`): correct for a loop whose
  tape slice starts at 0 (the common "recorded loop = whole tape" case); `tapeIn != 0`
  wraps to 0 instead of tapeIn. Refine to slice-relative wrap later.
- **Seek-on-drain latency**: a transport jump is honored only after the ~1s ring
  drains (compounds with the 10ms resolve cadence).
- **Multi-tape phrases**: only the FIRST leaf loop's tape is wired per pill.
- **Unrecorded-tape silence**: a pill whose tape file doesn't exist stays silent
  until a later `refreshOutputMixerPhraseChannels` re-opens it (no auto-retry).
- Prefetch ring fixed ~1.0 s (tier-coupled depth deferred).

## After T0b: next (own session)

T0b closes the load-bearing "phrase channels are silent" gap. Candidates next:
- Resume engine milestones (M8 S7+) per the V7/V10 order, OR
- The OPERATOR-RAISED arrangement subsystem (in `todo.md` 2026-05-30): (1) phrase
  playback + arrangement model/UX, (2) "pruning" (operator will DEFINE — capture,
  don't infer), (3) phrase-boundary crossfades (MIDI/audio/video), (4) export of an
  arranged phrase timeline (§6.11 "render"). Await a dedicated design session.
- Tighten a v1 limit if the operator's ear-test surfaces one (slice-relative wrap,
  seek latency, or the unrecorded-tape auto-retry are the likely first asks).

## Method note (continue this if iterating the same way)

Subagent-driven (`superpowers:subagent-driven-development`): one `general-purpose`
implementer (model `sonnet`) per task, FULL task text pasted in (don't make it read
the plan file). Per task: implement → spec-compliance review → code-quality review →
fix loop → git-verify (real `git log` + re-run the named suite whose summary line you
read) before advancing. The harness emits STALE clangd diagnostics ("file not found",
"no member", Catch operand errors) for freshly-edited headers/tests — trust the real
Ninja build, not the diagnostic. Subagents stage ONLY their task's files (never
`git add -A` — the build emits a stray gitignored `tests/IdaTests`). Single-line
`feat:`/`fix: T0b — …` commits. Work on `master`; push authorized. Two genuine
plan-stage corrections caught during execution: `Rational` has no `fromDouble` (use
`Rational(llround(sec*sr), llround(sr))`), and the prefetcher must locate records by
the index `lmcTs` (variable-block reality), not a fixed framesPerRecord.

## Commands to run first (next chat)

```bash
cd /Users/larryseyer/IDA
git log --oneline -6          # expect 7ac220e at HEAD, in sync with origin/master
git status --short            # expect only external/sfizz
cmake --build build --target IdaTests && ./build/tests/IdaTests "[tape-playback]"  # 21 cases / 790 assertions
```
