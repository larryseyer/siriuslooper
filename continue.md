# Session Continuation — 2026-05-30 (NEXT CHAT: blank-slate + phrase-creation flow)

## ⭐ The essential next topic (operator-set — START A FRESH CHAT FOR THE DESIGN)

**How phrases are CREATED is the thing that blocks testing IDA, and the current
starting state is the wrong approach.** Today IDA boots into a pre-authored
**demo song** (`app/DemoSession.cpp` — `buildDemoSession()`): a fixed
intro/verse/verse/verse/outro arrangement with pre-existing phrases and tape
references. The operator's directive:

> We need a **blank slate** — no demo song, no phrases, no tapes. Let the tester
> walk **all** the steps to go from first launch of IDA to actually using it.

So the next chat is a **design session** (use `superpowers:brainstorming`) on the
operator-facing first-run flow:

1. First launch = empty project. Minimal default mixers only (per
   [[project_minimal_default_mixers]]): channels matching physical I/O, the pool
   floor of exactly one primary tape (`TapeId{1}`), **no** phrases, **no**
   pre-authored song.
2. The tester records → audio lands in a **tape** (never "into a phrase").
3. The tester then **creates a phrase** as a structure-layer view over recorded
   tape region(s) — THIS is the missing/undesigned step. A phrase is just a
   length + in/out references into tape(s) (loops, single regions, anything);
   it's assigned *after* capture (even sub-ms after). There is currently **no UI
   gesture to create a phrase from a recorded tape** — that's the gap.
4. The new phrase then appears as an Output-Mixer strip and plays back (the T0b
   playback path already works once a phrase references a recorded tape).

Do NOT keep patching the demo session. The demo fixture should be **retired**
(or reduced to a true empty default) and replaced by a real create-a-phrase flow.
Capture this as the headline before touching code.

**Hard terminology rule the operator drove home (now in memory
[[project_close_stops_all_tapes_single_instance]] sibling — see below):** inputs
assign to **TAPES**, never phrases. Phrases are structure-layer views shown on
the Output Mixer / timeline, never in any input-mixer assignment UI. Never frame
recording as going "into a phrase."

## Repo State (verified)

- **IDA** master HEAD = **`6b199b0`**, pushed, in sync with origin/master.
  Clean tree except `external/sfizz` (pre-existing untracked — leave it).
- **OTTO** submodule UNCHANGED this session; no SHA bump. Inbox `[FROM OTTO →
  IDA]` empty. The `[FROM IDA → OTTO]` entries are `needs-ack` for OTTO's Claude.
- An IDA instance (single-instance enforced) may still be running from this
  session's testing — it records continuously to the tape store while open;
  close it when done (that finalizes the tape).

## What this session actually did (debugging the T0b ear-test)

The operator's T0b ear-test (record → play a phrase) was **silent**. Root-caused
and fixed four things (all pushed); the *fifth* finding is the blank-slate
redirect above.

1. **`e61b892` — tape-id mismatch (the silence).** The demo's phrase leaf-loops
   referenced `TapeId 100/200/300/400`, but live capture commits input strips to
   the pool's **primary `TapeId{1}`**. Nothing recorded to 100–400, so the
   playback resolver opened `tape-100.idatape` (never created) → every phrase
   channel silent. Fix: demo loops + inputs remapped to ids 1..4, and the
   `TapePool` is now **seeded from the session inputs** so each tape a phrase
   reads is a real capture destination. (`DemoSession.cpp`, `MainComponent.cpp`.)
   The T0b engine path itself was never broken (21 `[tape-playback]` cases green).
2. **`85e7ee1` — single instance / tape-close-on-exit (operator hard rule).**
   `Main.cpp` `moreThanOneInstanceAllowed()` was `true`; two copies each pointed
   their always-running `TapeRecordWriter` at the SAME `tape-1.idatape`, racing
   delete/reopen → the file ballooned (saw **948 MB**) and closing one window
   left the other recording. Now **single-instance** (second launch routes to the
   existing window via `anotherInstanceStarted`). Clean close already finalizes
   every tape via the `MainComponent`→`TapeRecordWriter` dtor chain — verified by
   code + empirically. **Never `kill -9` the app when testing tapes** (SIGKILL
   skips the dtor). Saved as memory [[project_close_stops_all_tapes_single_instance]].
3. **`942ba5b` — tape store path.** Tapes were landing in `~/Library/IDA/tapes`,
   not `~/Library/Application Support/IDA/tapes`: `juce::File::
   userApplicationDataDirectory` is `~/Library` on macOS and the helper omitted
   "Application Support". New shared `idaAppSupportDirectory()` adds it on
   macOS/iOS only (Windows/Linux keep `<userAppData>/IDA`). Fixes both the tape
   store and the calibration sidecar. No migration (operator: no users, no
   back-compat). **Tapes now live at `~/Library/Application Support/IDA/tapes`.**
   (Old `~/Library/IDA/tapes` is orphaned — delete it manually if desired; I did
   not, deletion is a destructive op I wasn't authorized for.)
4. **`6b199b0` — demo tapes named "Tape N", not after phrases.** The input
   destination picker correctly lists `tapePool_.tapes()`, but seeding tape
   *names* from the input displayNames ("Intro pad", "Verse rhythm") made the
   picker read like inputs were assigned to phrases. Renamed demo pool tapes to
   "Tape 1..4". **Unverified by eye** (operator pivoted to the blank-slate topic
   before confirming) — but the picker now lists "Tape 1..4" + buses.

## Key code anchors (for the blank-slate work)

- `app/DemoSession.cpp` — `buildDemoSession()`: the fixture to retire/replace.
- `app/MainComponent.cpp`:
  - `~`81 `tapesDirectory()` / `idaAppSupportDirectory()` — tape store root.
  - `~`4254 `TapeRecordWriter` construction (`tapesDirectory()`); `~`4285 pool
    seeding from `demo_.inputs` + `mirrorTapePool`.
  - `7895 rebuildInputStrips()` — input strips are built from **physical
    `inputPairs_`** (names "In 1", "In 1-2"), each defaulting to
    `CommitToTape`→primary tape. NOT from `demo_.inputs`.
  - `7729 refreshInputDestinations()` — input destination picker = `tapePool_`
    tapes + buses (correct: inputs→tapes).
  - `7079 refreshOutputMixerPhraseChannels()` — builds one Output-Mixer phrase
    strip per pill + opens a `TapePrefetcher` per phrase's leaf-loop tape.
- `core/include/ida/TapePool.h` — default ctor seeds one tape `TapeId{1}` "Tape 1".
- Canonical "why": `docs/IDA_Whitepaper_V10.md` (data layer = Tape, structure
  layer = Constituent/phrase, §7.2 split). T0b design:
  `docs/superpowers/specs/2026-05-30-render-path-and-tape-store-design.md`.

## Known v1 playback limits (still true; from the T0b session, in `todo.md`)

FreeRunning leaf loops only; identity device calibration; playhead = elapsed-
playing-seconds (no OTTO seek/relocate yet); prefetcher wrap-from-0; seek-on-
drain ~1 s latency; first leaf-loop tape only per pill; **unrecorded-tape
silence + no auto-retry** (a phrase whose tape file didn't exist at channel-open
stays silent until the next `refreshOutputMixerPhraseChannels`); ~1.0 s ring.
The unrecorded-tape/auto-retry limit interacts badly with live record→play in a
single run (the prefetcher opens at launch; the always-running writer deletes +
recreates the tape on first write per session) — worth designing alongside the
phrase-creation flow.

## Commands to run first (next chat)

```bash
cd /Users/larryseyer/IDA
git log --oneline -6          # expect 6b199b0 at HEAD, in sync with origin/master
git status --short            # expect only external/sfizz
ls -la "$HOME/Library/Application Support/IDA/tapes"   # tape store (new path)
```
Then START A FRESH BRAINSTORM on the blank-slate first-run + phrase-creation flow
before any code.
