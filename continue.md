# Session Continuation — 2026-05-30 (T0a tape-record-store, Tasks 1–2 landed)

## Current State

Implementing **Tier-0 / Phase T0a — the durable, media-agnostic tape record
store** via `superpowers:subagent-driven-development` against a committed plan.
**Tasks 1 and 2 of 9 are done, verified, and committed.** The next chat resumes
at **Task 3** and runs through **Task 9**.

- **Plan (committed):** `docs/superpowers/plans/2026-05-30-tape-record-store.md`
  (commit `9d7f468`). Full bite-sized TDD steps + authoritative byte layout +
  authoritative API signatures for every task. **Read it first** — it is the
  single source of truth for Tasks 3–9; do not re-derive.
- **Design spec (committed earlier):**
  `docs/superpowers/specs/2026-05-30-render-path-and-tape-store-design.md`
  (the "why"; whitepaper §8.1–8.5, §15.2, §17.8, §17.9).
- **Chat plan file (the tier roadmap):**
  `/Users/larryseyer/.claude/plans/read-continue-and-begin-sleepy-oasis.md`
  (T0a detail + T0b outline + full IDA tier roadmap T0→T5).

## Repo State (verified against `git log`, not narration)

- **IDA** master HEAD = **`55495b3`**. Clean except `external/sfizz` (pre-existing
  untracked content — leave it). **Nothing pushed this session.**
- Commits this session, in order:
  - `9d7f468` docs: T0a plan
  - `12256e9` feat: T0a — TapeRecord byte layout + CRC32 (Task 1)
  - `aeed77f` docs: todo — operator's arrangement/pruning/crossfade/export topics
  - `55495b3` feat: T0a — IPayloadCodec interface + codec registry (Task 2)
- **OTTO** submodule UNCHANGED this session. No `external/OTTO/` edits, no SHA bump.
- Inbox (`external/OTTO/CROSS_PROJECT_INBOX.md`): `[FROM OTTO → IDA]` empty; the
  `[FROM IDA → OTTO]` J-A + EventBus entries are `needs-ack` addressed to OTTO's
  Claude — **not ours to ack or prune.**

## ⚠️ CRITICAL — harness instability this session (read before trusting anything)

The tool output channel **intermittently fabricated and replayed stale results.**
It manufactured six convincing "DONE" subagent reports for Tasks 3–8 (climbing
assertion counts, plausible commit SHAs like `e9d2a14`/`7b3e1f0`/`d2b8e15`) for
work that **never happened** — those files are not on disk and those commits do
not exist. It was caught only by reading `git log` directly.

**Operating rule for the next chat:** after EVERY subagent reports DONE, and
after every build/test claim, **verify against real `git log --oneline` + `ls`
of the expected files + a re-run of `./build/tests/IdaTests "[tape-record]"`
whose summary line you actually read.** Redirect Bash/Read to a `/tmp` file and
read that when output looks empty or suspicious. Never advance a task or commit
on an unconfirmed green. (This is the standing continue.md rule, made acute.)

## What is ACTUALLY done (verified on disk + in git at `55495b3`)

- **Task 1 — `core/include/ida/TapeRecord.h` + `core/src/TapeRecord.cpp`**
  (`12256e9`): file-header (`"IDATAPE\0"` magic + u16 version=1 + u16 reserved,
  `kTapeFileHeaderBytes=12`), record body layout (`kRecordHeaderBytes=44`:
  seq u64, type u16, codec u16, conceptual Rational, lmc Rational), little-endian
  `writeLE16/32/64`+`readLE16/32/64`, standard CRC-32 (IEEE 802.3,
  `crc32("123456789")==0xCBF43926`), `writeFileHeader`/`readFileHeader`,
  `encodeRecord` (`[u32 bodyLen][body][u32 crc32(body)]`), `decodeRecordBody`.
  Enums `TapeRecordType{Audio=1}`, `TapeCodecId{None=0,AudioFlac=1,AudioPcm=2}`.
  Registered in `core/CMakeLists.txt`.
- **Task 2 — `engine/include/ida/IPayloadCodec.h` + `engine/src/IPayloadCodec.cpp`**
  (`55495b3`): `PcmBlock{vector<float> left,right; numFrames()}`, `IPayloadCodec`
  (`codecId()`, `encode(L,R,n,sr)→vector<byte>`, `decode(bytes,len,PcmBlock&)→bool`),
  `TapeCodecRegistry` (`registerCodec` keyed by `(u16)codecId`, replaces on dup;
  `codecFor(id)→IPayloadCodec*` or nullptr). Registered in `engine/CMakeLists.txt`.
- **Tests:** `tests/TapeRecordStoreTests.cpp` (tag `[tape-record]`), registered in
  `tests/CMakeLists.txt` after `FlacTapeSinkTests.cpp`. **16 cases / 42 assertions,
  all pass** at `55495b3` (verified, not narrated).

## What REMAINS — Tasks 3–9 (resume here; each is fully specced in the plan)

3. **Audio codecs** — `audio/include/ida/AudioPayloadCodec.{h,cpp}`: `FlacAudioCodec`
   (self-contained 24-bit FLAC-per-block via `juce::MemoryOutputStream`+
   `FlacAudioFormat`, level 3; decode via `MemoryInputStream`) + `PcmAudioCodec`
   (`[u32 numFrames]`+interleaved LE float32, bit-exact). Reference the FLAC writer
   API in `audio/src/FlacTapeSink.cpp:122-181`.
4. **`TapeRecordWriter`** (`audio/`) — an `ITapeSink` that **reuses FlacTapeSink's
   audio→SPSC→worker pattern verbatim** (POD Message, `LockFreeSpscQueue`, worker
   thread, `wakeCv_`/`wait_for`, drain, dtor join+drain). Worker frames each block
   as a record, lazily opens `<dir>/tape-<id>.idatape` (writes the 12-byte header
   first), flushes at `flushIntervalMs`. Derives `lmcTs = Rational(framesWritten,
   round(sr))`, `conceptualTs == lmcTs` at capture (documented v1 limit — the real
   conceptual↔LMC map is a T0b render-time TempoMap concern). **Mirror FlacTapeSink's
   public surface** (`setSampleRate`, `closeTape`, `droppedBlockCount`) for the
   Task-9 drop-in swap; add `flushIntervalMs()`, `tapeFile(id)`.
5. **`TapeRecordReader`** (`audio/`) — `open(file, registry, report, recover=true)`,
   private `scanFrom(offset, recover, report)` scan core, `recordCount()`, `index()`
   (vector<RecordIndexEntry>), `readAudioRecord(pos, PcmBlock&, header&)` (random
   access by record position via the index — must NOT decode predecessors), `refresh`.
   This task = clean-file scan + random access; leave recover/refresh seams for 6/7.
6. **Crash recovery** — `recover=true` truncates a partial/CRC-bad trailing record
   at its start, keeps the valid prefix, fills an honest `TapeTruncationReport`
   (§17.9). `recover=false` must NOT modify the file (precondition for Task 7).
7. **Concurrent read-during-write** — harden `refresh` so a `recover=false` reader
   reads already-flushed records while the writer appends; never exposes a partial
   record; never writes. Monotonic index growth.
8. **Tier flush cadence** — add `int flushIntervalMs` to `TierPolicy` in
   `app/CapabilityTier.h` (Lavish=1, Comfortable=50, Tight=200, Survival=1000, per
   §17.8) + `policyFor`; writer already specced to clamp `[1,5000]`. Watch the
   `TierPolicy` aggregate-init sites in `tests/CapabilityTierTests.cpp`.
9. **Live swap + retire FlacTapeSink** — in `app/MainComponent.cpp:4244` construct
   `TapeRecordWriter` (codec = `AudioPcm` at Lavish else `AudioFlac`; flush =
   `policyFor(tier).flushIntervalMs`); rename member `flacTapeSink_`→`tapeRecordWriter_`
   (`MainComponent.h:371`, include `:15`); keep `setSampleRate`/`droppedBlockCount`/
   `closeTape` call sites (`:6078`/`:6085`). **Delete** `audio/include/ida/FlacTapeSink.h`,
   `audio/src/FlacTapeSink.cpp`, `tests/FlacTapeSinkTests.cpp`; drop from `audio/CMakeLists.txt`
   + `tests/CMakeLists.txt`; fix the stale `FlacTapeSink` naming comments in
   `engine/include/ida/TapeColoringSink.h:22/51` + `engine/CMakeLists.txt:28`.
   `grep -rnE "TODO|FIXME|XXX|stub|placeholder"` changed files → zero. **Clean build**
   (`rm -rf build`) + full `ctest` green before any operator handoff.

### Integration topology (so Task 9 swaps cleanly)
`InputMixer.setTapeSink(ITapeSink*)` → `TapeColoringSink` (TAPECOLOR decorator) →
the sink. MainComponent owns the sink (`flacTapeSink_` today → `tapeRecordWriter_`).
`ITapeSink`, `InputMixer`, `TapeColoringSink` are UNCHANGED by T0a — only the
concrete sink swaps. `tapeColoringSink_`'s inner pointer must point at the new writer.

## Execution method (continue exactly this)

Subagent-driven: dispatch one `general-purpose` implementer (model `sonnet`) per
task with the FULL task text from the plan pasted in (don't make it read the plan
file). Enforce per-task: **explicit `git add` of only the task's files — NEVER
`git add -A`** (the build emits a stray `tests/IdaTests` binary; it is gitignored
now via `/tests/IdaTests`, but stay disciplined). After each implementer: spec
review → code-quality review → fix loop → mark complete → **git-verify** before
next task. Single-line commit messages `feat: T0a — <title>`. Work on `master`
(authorized). Commit per task; push is operator-authorized but optional — confirm
intent at session end.

## Open design topics captured this session (NOT in the T0a plan — own future session)

In `todo.md` (commit `aeed77f`), operator-raised 2026-05-30, await a dedicated
arrangement-subsystem design session AFTER T0b:
1. How phrases are **played back AND arranged** (the arrangement model/UX — undiscussed).
2. **"Pruning"** — operator will DEFINE this later; capture the term, do not infer.
3. **Phrase-boundary crossfades** for MIDI, audio, AND video.
4. **Export of an arranged phrase timeline to disk** (bounce/materialize; §6.11 "render").

## After T0a: T0b (next plan, own session)

Playback-resolution path — RT-safe OTTO-transport playhead accessor → off-thread
`RenderPipeline::activeReadsAt` snapshot → `TapeReader` worker prefetch (on T0a's
reader) → new audio-callback playback step (`AudioCallback.cpp` between OTTO render
~line 112 and OutputMixer dispatch ~118) filling each phrase channel's scratch via
`setChannelAudioSource` (MON template, `InputMixer.cpp:227-242`). v1: identity
calibration, FreeRunning leaf loops only. Outline in the chat plan file.

## Two-terminal note (operator confirmed OTTO terminal is live)

A second Claude is working on OTTO concurrently. T0a is IDA-only and safe — it
touches no `external/OTTO/` source and bumps no submodule SHA. While that terminal
is live: do NOT edit OTTO source or bump the OTTO gitlink from this side; if an
OTTO change is ever needed, REQUEST it via the inbox. T0a needs none.

## Commands to run first (next chat)

```bash
cd /Users/larryseyer/IDA
git log --oneline -6        # expect 55495b3 at HEAD (Task 2)
git status --short          # expect only external/sfizz
ls audio/include/ida/AudioPayloadCodec.h 2>/dev/null || echo "Task 3 not started (expected)"
cmake --build build --target IdaTests && ./build/tests/IdaTests "[tape-record]"   # expect 16 cases / 42 assertions
# Then: read the plan doc, dispatch Task 3, git-verify every step (harness was unstable — trust git, not narration).
```
