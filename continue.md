# Session Continuation — 2026-05-30 (render-path design session)

## Current State

This session produced **(1)** a whole-of-IDA completion roadmap and **(2)** an
approved, committed design spec for the first sprint: the **render path + a
durable media-agnostic tape store (Tier 0)**. No code was written yet — the next
chat **starts implementation** via the `writing-plans` skill on the spec.

- **Design spec (committed, local — see Repo State for push status):**
  `docs/superpowers/specs/2026-05-30-render-path-and-tape-store-design.md`
  (commit `a56738a`).
- **Roadmap** (tiers below) was approved in plan mode; its working copy is the
  chat plan file, but the durable form is the tier list in this handoff.

## What Was Done This Session

- Read `continue.md` + the OTTO inbox. **Inbox needs no IDA action:**
  `[FROM OTTO → IDA]` is empty; the two `[FROM IDA → OTTO]` J-A entries are
  `needs-ack` addressed to OTTO's Claude (do **not** prune, not ours to ack).
- Audited current state. **Keystone finding:** the render path is unwired —
  `RenderPipeline::activeReadsAt` has zero callers outside tests, there is no
  tape **reader** (tape subsystem is write-only), and `setChannelAudioSource` is
  called only for OTTO strips, so **Output-Mixer phrase channels are silent**.
  Every per-phrase capability (level/pan/FX/sends/meter, plus the queued solo +
  INS-FX work) processes silence until the render path lands.
- Operator steering, mid-session: **(a)** "be driven by the white paper" →
  grounded the design in §3.6 / §5.2 / §6.6 / §8.x / §15.2 / §17.8. **(b)** harden
  the tape system — flush RAM→disk at intervals, **always play from permanent
  storage**, never the volatile ring. **(c)** the container must be
  **media-agnostic** (audio first, then MIDI/video/text/html/docs — media-agnostic
  looping), not FLAC-specific (FLAC is audio-only).
- Brainstormed (superpowers:brainstorming) → wrote + committed the Tier-0 spec.

## Key Decisions (the spec's resolved forks)

| Decision | Choice |
|----------|--------|
| Playhead source | **OTTO transport** (positionInSeconds/isPlaying); IDA LMC stays pure time (§5.7) |
| Reader architecture | **Worker-driven streaming reader from disk**; audio thread only pulls lock-free PCM (RAM-only reader rejected — not whitepaper-true) |
| On-disk tape format | **Uniform self-delimiting checksummed record container** (§17.8), media-agnostic, per-type payload codecs. FLAC is only the *audio payload codec* |
| First codec | **Audio** (FLAC block; PCM at Lavish). MIDI/video/other = next sprint |
| Slice split | **T0a (storage hardening) → T0b (reader + render-path wiring)** |

## The Sprint (next chat starts here)

Invoke `writing-plans` on the spec; likely **two plans** (each phase is
substantial), TDD + headless:

- **T0a — Durable media-agnostic tape store.** Append-only record container
  `[len | seq | type-tag | conceptual-ts | lmc-ts | checksum | payload]`;
  open-ended payload-type registry; **audio codec only** this sprint; tier-aware
  flush (§17.8 table, 1–5000 ms override); crash-recovery scan-and-truncate;
  per-tape index for concurrent random-access read; RAM ring stays volatile
  scratch (disk is sole playback source). Reuse `FlacTapeSink`'s
  audio-thread→SPSC→worker write path; generalize `ITapeSink` to typed payloads.
- **T0b — Playback-resolution path (on T0a).** RT-safe `OttoHost` transport-
  position accessor → playhead; `RenderPipeline::activeReadsAt` runs on a worker
  (it allocates/walks the tree) and publishes a lock-free active-reads snapshot;
  a `TapeReader` worker prefetch-decodes ahead of the playhead into per-channel
  PCM rings; a **new audio-callback playback step** (between OTTO render Step 2b
  and OutputMixer Step 3) pulls samples into each phrase's stable scratch buffer;
  `setChannelAudioSource` points the channel at the scratch (once). Output side
  (ChannelStrip FX/sends/routing/meter) already built — render path ends at the
  scratch. **v1 limits:** FreeRunning leaf loops only (M3 reality), identity
  calibration.

Open plan-stage questions are listed at the end of the spec (record byte
layout/versioning; index sidecar vs scan-on-open; resolution cadence + prefetch
depth; refactor `FlacTapeSink` in place vs new writer).

## Full IDA completion roadmap (tiers — keep the bigger sequence)

- **Tier 0 (this sprint)** — render path: T0a tape store + T0b reader/wiring.
- **Tier 1** — Output-Mixer completion (now demonstrable once Tier 0 lands):
  cross-group solo; output-channel INS FX (fix the latent shared-host node-key
  collision — input `BusId{1}` and output `BusId{1}` already collide in the
  shared `OutOfProcessEffectChainHost`); output-graph save/load into production;
  sends → FX returns + internal RVB/DLY DSP.
- **Tier 2** — file-input source kinds (OTTO import gate now satisfied):
  transport-sync, MIDI, **video** (operator asked about video — it's unblocked,
  lands here as its own brainstorm→spec→plan; or parallelizable since it's
  independent of Tier 0/1).
- **Tier 3** — render-to-parts / timeline / finished-song (§6.11 future; builds
  on Tier 0).
- **Tier 4** — plugin hosting (scanner is broken at every entry point — fix root
  cause, then out-of-proc isolation + the insert-chain picker UI).
- **Tier 5** — iOS / perf (idle ~22% profiling), AUv3 port, notary/signing,
  marketing site.

## Repo State

- **IDA** master `a56738a` (this session's spec commit on top of `56d19ea`).
  `external/sfizz` remains dirty (pre-existing untracked content — leave it).
- **lsfx_tapecolor** `22f736f`. **OTTO** (submodule) `38aa2101`. No OTTO-side
  change this session.
- Push status: **set at session end** — see the last commit's push.

## Context the Next Session Needs

- **Read `continue.md` first, then `external/OTTO/CROSS_PROJECT_INBOX.md`.**
  Inbox `[FROM OTTO → IDA]` empty; the J-A `[FROM IDA → OTTO]` entries are not
  ours to ack/prune.
- **Whitepaper-driven** is an explicit operator directive — ground every choice
  in `docs/IDA_Whitepaper_V10.md` (here: §3.6, §5.2, §6.6, §8.1–8.5, §15.2,
  §17.8) before deviating to a code-level pragmatic read.
- **RT-safety** (`docs/RT_SAFETY_CONTRACT.md`): the audio thread does only atomic
  reads + lock-free ring pulls + memcpy; all alloc/decode/I/O/tree-walk on
  worker/message threads.
- **Clean builds** (`rm -rf build`) before any operator GUI handoff; engine work
  is headless-TDD. Recurring harness glitch: Bash/Read sometimes return EMPTY
  (not errored) — redirect to `/tmp` and Read it; never trust a clean-looking
  empty result; never commit on an unconfirmed green.
- The audit's full current-state maturity table and the node-key-collision
  detail are in this session's reasoning; the load-bearing facts are captured
  above and in the spec.

## Commands to Run First

```bash
cd /Users/larryseyer/IDA
git status                  # expect clean except external/sfizz (pre-existing)
git log --oneline -3        # confirm a56738a (spec) is present
# Then: read the spec, invoke writing-plans, start T0a.
```

## Open Questions

- None blocking. The spec's "Open questions for the plan stage" are the first
  things the `writing-plans` session resolves.
