# File input as a first-class Input Mixer source (playlist player)

**Date:** 2026-05-25
**Status:** approved — proceeding to plan
**Source:** chat session 2026-05-25 — operator cannot verify mixer/tape
behavior without a working audio interface (M4 + Sequoia microphone
pipeline produces 24 kHz capture with ~500 ms latency on a 16-sample
buffer). File input has been a first-class source in the whitepaper since
V8 §6.6 / §7.2; this slice promotes it from spec text to working feature
and extends it from single-file to multi-file playlist (live-venue
setlist use).
**Memory pins:** `[[project_minimal_default_mixers]]`,
`[[project_input_source_mono_stereo_rme]]`,
`[[feedback_gesture_over_ui_clutter]]`,
`[[feedback_ios_long_press_pairs_right_click]]`,
`[[feedback_sirius_done_right_and_complete]]`,
`[[feedback_no_deferral_get_it_done]]`,
`[[project_two_mixers_totally_separate]]`,
`[[project_user_guide_alongside_whitepaper]]`
**Whitepaper:** V9 §6.6 (input mixer accepts four signal types, file
inputs first-class), §7.2 (input-layer file transport: start, rate, loop
region). Glossary entries "File I/O", "Input mixer". This slice extends
the spec from single-file to playlist semantics.

---

## 1. Background

The whitepaper has always specified file inputs as one of four first-class
signal kinds at the input mixer (alongside live audio, live MIDI, live
video). V9 §6.6 places file transport at the input layer, not the channel
layer, and §7.2 lists "start, rate, loop region" as input-layer file
decisions. Despite this, the engine has never had a `FileInput` value in
`InputKind`, the input mixer has never accepted a file source, and there
is no UI surface for picking or transporting one.

Three orthogonal forces motivate landing it now:

1. **Operator verification of any audio-touching slice is currently
   gated on hardware.** A working audio interface is the only way to
   exercise the mixer / tape / MON / routing chain audibly. The
   operator's M4 MacBook Pro under Sequoia (no audio interface
   currently attached) cannot supply clean test signal — Sequoia's
   microphone pipeline re-rate-converts to 24 kHz and inserts voice-
   processing latency that masks anything we'd actually want to
   verify. File input as a deterministic test signal source removes
   the gate.
2. **Live-venue use is the natural musical purpose.** A musician
   playing along to a backing track is the canonical professional
   case. A single-file MVP would underserve this; live use is
   typically a *set* of backing tracks played in order across a
   performance, with the performer-or-tech editing the list between
   songs.
3. **Per the whitepaper, file input is already inside the architecture.**
   Promoting it is not new architecture; it is implementing existing
   spec. The only architectural extension this slice introduces is
   "playlist of 1+ files" replacing the implicit single-file model —
   a one-sentence whitepaper amendment.

This slice ships file input properly per
`[[feedback_sirius_done_right_and_complete]]`: it lands as a full
working feature, not a throwaway test stub.

---

## 2. Whitepaper amendments (lands FIRST, before any code)

The only conceptual change is that a file input is now a **playlist of
one or more files**, edited live during playback. Three narrow edits:

1. **§6.6 (input mixer)** — wherever "file inputs" is introduced, add a
   short clause: *"A file input is an ordered playlist of one or more
   files; the playlist may be edited (add / remove / reorder) live
   during playback."*
2. **§7.2 (input-layer file transport)** — clarify that transport
   applies to whichever entry of the playlist is currently active, and
   that advance to the next entry is governed by **playlist scope**
   (off / track / list).
3. **Glossary** — add **Playlist scope** entry with the three values
   (Off / Track / List). Update the **File I/O** glossary entry to
   mention "playlist of one or more files" rather than the implicit
   single-file framing.

Everything else in this spec is implementation / UX / persistence detail
and belongs in *this* document, not in the whitepaper.

---

## 3. Scope (in vs. out, v1)

**In v1:**

- New `InputKind::FileInput` enum value (`core/include/ida/InputKind.h`).
- `FileInputDescriptor` (core) + `PlaylistEntryId` strong-typed handle.
- `FileInputSource` (audio/) owning a buffering reader, optional
  resampler, SPSC ring, and a worker-thread transport loop.
- WAV + AIFF + FLAC reader support (all three are first-class JUCE
  formats).
- Mono → dual-mono at the reader; stereo as-is; >2 channels → L+R only
  with dev-log warning.
- Transparent SR resampling when file SR ≠ device SR.
- Input Mixer blank-area gesture: **"Add file input…"** (alongside the
  existing "Add aux bus"). Right-click on desktop, long-press on iOS.
  Opens system file picker with multi-select on; chosen files become
  the initial playlist (1+ entries).
- Auto-creates an Input Mixer channel strip for the new file input.
  Identical strip behavior to a device-input strip: gain, MON, tape,
  routing all stock.
- Floating **File Player Window** (one per file input, no cap):
  - Title bar = displayName.
  - Transport bar: play / pause / stop (= pause + seek 0).
  - Playhead scrubber: live position read-out + scrub-to-seek (seek
    issued on mouse-up).
  - Loop-scope cycle button: ∅ (off) → ↻ track → ↻ list → ∅.
  - Track list: ordered rows of `{ # | filename | duration }`, current
    row highlighted, drag-handle for reorder, per-row `–` button
    disabled on the current-playing row only.
  - `+` button: opens system picker (multi-select) to append more
    tracks.
  - **Window opacity** — defaults to 92% (subtle translucency so the
    operator can see what's behind without losing readability).
    Per-window override via right-click → `Window opacity ▸` with five
    presets (60% / 75% / 85% / 92% / 100%) plus `Custom…` opening a
    small inline slider. Persisted per file input. Rationale: IDA has
    no Preferences panel today; per-window control is self-contained
    and matches the QuickTime-style window independence. When a
    Preferences panel eventually lands, the *default* value moves
    there; per-window override stays.
- macOS Window menu: **"File Players"** submenu listing every open
  file input by displayName; selecting brings the window forward.
- Strip recall: right-click strip → **"Show player…"** reopens the
  window from current engine state (works on macOS + iOS).
- Live playlist editing during playback:
  - Reorder: any row, including currently-playing.
  - Add: anytime.
  - Remove: any row EXCEPT the currently-playing one.
- LoopScope = { Off, Track, List }:
  - Off + EOF + more entries → advance.
  - Off + EOF + last entry → stop.
  - Track + EOF → seek same entry to 0.
  - List + EOF → advance, wrapping last → first.
- Persistence: file paths + loop scope into session JSON. Transport
  state (isPlaying, currentEntry, playhead) NOT persisted — every load
  starts stopped at entry 1.
- Missing-file handling: graceful at load time (row marked missing)
  and at mid-playback advance (worker skips, transport continues).
- Headless tests for all engine behaviors (TDD applies).

**Out of v1 (explicit non-goals; queued for follow-on slices):**

- **LMC / session-transport sync** — v1 has per-file-input independent
  transport (whitepaper-compliant). Slaving all file inputs to a session
  play button is a future enhancement once transport itself ships
  (`[[project_mixer_then_transport_roadmap]]`).
- **Gapless inter-track playback** — v1 closes the current reader and
  opens the next, producing a brief gap (~50 ms typical at 48 kHz
  stereo). Acceptable for setlists with talk between songs; not
  appropriate for continuous-mix DJ use. Gapless = dual-reader handoff
  pattern, a separate slice.
- **Loop in/out points within a file** — v1 loops whole files only.
- **Tempo / pitch warping** — v1 plays files at native rate.
- **MP3 / OGG / M4A / compressed formats** — v1 is uncompressed / FLAC
  only. Compressed formats need extra JUCE format registration and
  aren't the right delivery quality for pro-audio backing tracks.
- **>2-channel routing** — v1 takes L+R from multi-channel files.
- **Many-to-one channels-from-one-source fan-out** — the descriptor
  model supports it (whitepaper allows it), but v1 wires 1:1
  channel↔file-input only.
- **Drag-and-drop file from Finder onto the mixer** — v1 is system
  file picker only.
- **Saving / restoring playhead position across reloads** — v1 always
  starts stopped at entry 1.
- **Operator-facing user guide entry for file input** — depends on the
  user guide existing first (`[[project_user_guide_alongside_whitepaper]]`).

---

## 4. Architecture

### 4.1 Layer ownership

| Layer | Owns | Does not own |
|---|---|---|
| `core/` (JUCE-free) | `InputKind`, `FileInputDescriptor`, `PlaylistEntryId`, `LoopScope` enum | Anything that reads bytes |
| `audio/` | `FileInputSource` (reader, ring, worker thread, transport) | InputMixer wiring |
| `engine/` | `InputMixer` registration + transport surface + channel attachment | Disk I/O |
| `persistence/` | JSON read/write of file-input descriptors | Anything live |
| `ui/` | `FileInputPlayerWindow`, blank-area gesture, strip recall menu, Window menu entry | Engine state (window is a pure view) |

Lives in `audio/` (not `engine/`) because `FileInputSource` depends on
`juce::AudioFormatManager` / `juce::BufferingAudioReader`. `engine/`
stays JUCE-light (only `juce_core` PRIVATE per CLAUDE.md).

### 4.2 Core types (new)

```cpp
// core/include/ida/InputKind.h — add value
enum class InputKind { Audio, Video, Midi, Control,
                       ParameterAutomation, Transport, System,
                       FileInput };

// core/include/ida/PlaylistEntryId.h — new, house pattern
class PlaylistEntryId {
public:
    explicit constexpr PlaylistEntryId (std::int64_t v) noexcept : value_(v) {}
    constexpr std::int64_t value() const noexcept { return value_; }
    bool operator== (const PlaylistEntryId& o) const noexcept { return value_ == o.value_; }
    bool operator!= (const PlaylistEntryId& o) const noexcept { return !(*this == o); }
private:
    std::int64_t value_;
};

// core/include/ida/LoopScope.h — new
enum class LoopScope { Off, Track, List };

// core/include/ida/FileInputDescriptor.h — new
struct FileInputEntry {
    PlaylistEntryId entryId;
    std::string path;             // absolute path
    std::optional<int> durationFrames;  // cached on first open
    bool missing { false };       // set at load or at advance time
};

struct FileInputDescriptor {
    TapeId tapeId;                // back-reference, same pattern as InputDescriptor
    std::string displayName;      // operator-visible
    std::vector<FileInputEntry> entries;
    LoopScope loopScope { LoopScope::Off };
    float windowOpacity { 0.92f };  // clamped to [0.5, 1.0] on read
    ChannelDefaults defaults {};
};
```

`signalTypeOf(InputKind::FileInput)` returns `SignalType::Audio` (file
inputs are audio at the channel-layer level; MIDI/video file types are
out of scope for v1).

### 4.3 `FileInputSource` (audio/)

Owns the reader stack and the transport loop for one file input.

```
FileInputSource
  ├── juce::AudioFormatManager  (registers WAV/AIFF/FLAC at construction)
  ├── std::unique_ptr<juce::BufferingAudioReader>  currentReader
  ├── std::unique_ptr<juce::ResamplingAudioSource> resampler (if needed)
  ├── SpscRingBuffer  ring  (~250 ms stereo @ 48 kHz = ~96 KB)
  ├── std::mutex      listMutex      (UI ↔ worker)
  ├── std::condition_variable workerCv
  ├── std::vector<FileInputEntry>  entries        (under listMutex)
  ├── std::atomic<bool>         isPlaying
  ├── std::atomic<int64_t>      currentEntryIdValue  (-1 sentinel = none)
  ├── std::atomic<int64_t>      playheadFrames
  ├── std::atomic<LoopScope>    loopScope
  ├── CommandQueue (SPSC)       transportCommands  (play/pause/stop/seek/setCurrent)
  └── (the shared juce::TimeSliceThread owns the worker time-slice;
       FileInputSource registers itself as a TimeSliceClient.)
```

Threads:

- **Audio thread** — calls `pullInto(buffer, N)` which pops N frames
  from the ring. `noexcept`, no allocation, no locks, no I/O.
  Underrun: fills remaining frames with silence, increments an atomic
  underrun counter (dev-diagnostic only).
- **Worker thread** (shared `juce::TimeSliceThread` across all file
  inputs) — runs the transport loop documented in §4.4.
- **UI thread** — issues transport commands via the SPSC command
  queue; takes `listMutex` to mutate `entries`.

### 4.4 Worker transport loop

```
useTimeSlice():
  drain transport command queue:
    play  → isPlaying = true; cv.notify
    pause → isPlaying = false
    stop  → isPlaying = false; seek to 0 on current
    seek(entryId, frame) → reposition reader (open if entryId differs from current)
    setCurrent(entryId)  → close current reader; open new; playhead = 0
    setLoopScope(s) → loopScope.store(s)

  if !isPlaying: return shortTimeSlice (sleep on cv)

  if currentReader is null:
    open first non-missing entry under listMutex; if none → isPlaying=false; return

  if reader EOF reached:
    advance per loopScope (see §3 scope table):
      Off + last      → isPlaying=false; return
      Off + more      → open successor (under listMutex)
      Track           → seek reader to 0
      List            → open successor, wrapping
    if successor open fails (missing) → mark missing, try the next; if
       all remaining missing → isPlaying=false; return

  read up to ringHeadroom frames from reader into scratch
  apply resampler if file SR ≠ device SR
  ring.pushFrom(scratch, framesRead)
  playheadFrames.fetch_add(framesAdded)
  return shortTimeSlice (let other clients run)
```

"Successor" is computed under `listMutex` by finding `currentEntryId`
in `entries` and taking the next-non-missing-or-wrapping entry. Reorder
during playback works for free because lookup is by id, not by index.

### 4.5 `InputMixer` surface (engine/)

New public methods (mirror today's `registerInput` / channel-attach
pattern):

```cpp
// Registration / removal
InputId registerFileInput (const FileInputDescriptor& desc);
void    unregisterFileInput (InputId id);
const FileInputDescriptor* fileInputDescriptor (InputId id) const;

// Playlist mutation (UI thread; thread-safe internally)
PlaylistEntryId addFileInputEntry    (InputId id, std::string path);
bool            removeFileInputEntry (InputId id, PlaylistEntryId entry);
bool            reorderFileInput     (InputId id, PlaylistEntryId entry, int newIndex);

// Transport
void playFileInput  (InputId id);
void pauseFileInput (InputId id);
void stopFileInput  (InputId id);
void seekFileInput  (InputId id, int64_t frame);
void setCurrentFileInputEntry (InputId id, PlaylistEntryId entry);
void setFileInputLoopScope    (InputId id, LoopScope scope);
void setFileInputWindowOpacity (InputId id, float opacity);  // clamps to [0.5, 1.0]

// State read-back (UI thread polling at 30 Hz)
struct FileInputTransportState {
    bool             isPlaying;
    PlaylistEntryId  currentEntry;
    int64_t          playheadFrames;
    LoopScope        loopScope;
};
FileInputTransportState fileInputTransportState (InputId id) const;
```

Channels attach to a file input identically to a device input
(`addChannel(InputId, SignalType)`); the channel side of the mixer
treats the input as opaque.

Audio-callback patch (`InputMixer::processBlock`): for each file input,
pull from its `FileInputSource` ring into the per-channel input buffer
before today's gain / MON / tape / routing stages run. No other audio-
path change.

### 4.6 UI components

- **`FileInputPlayerWindow`** (`juce::DocumentWindow`, native title bar)
  - Owns: `TransportBar`, `PlayheadScrubber`, `CurrentTrackLabel`,
    `TrackListBox`, `AppendButton`, `LoopScopeButton`.
  - Polls `fileInputTransportState(id)` at 30 Hz for repaint.
  - Reads `FileInputDescriptor::windowOpacity` once on construction
    and applies via `setAlpha(opacity)`. Right-click menu offers
    `Window opacity ▸` with the five presets + `Custom…` slider;
    selection calls `InputMixer::setFileInputWindowOpacity` and the
    window's `setAlpha` immediately.
  - Closing (red X) destroys the window object only; engine state
    survives. Minimize (yellow) is OS-handled.
- **`InputMixerPane`** — blank-area menu extended:
  - `Add aux bus` (existing)
  - `Add file input…` (new) → multi-select system picker
- **`ChannelStripMenu`** (right-click strip) — for file-input strips
  only, adds `Show player…` reopening the window.
- **macOS menu bar — `Window` menu** — submenu `File Players` listing
  every open file input by displayName; click brings the window forward.
  iOS path skips this (no menu bar); strip recall is the only path.

### 4.7 Persistence

Session JSON gains an additive `fileInputs` array on each Input Mixer:

```json
"inputMixer": {
  "fileInputs": [
    {
      "displayName": "Setlist A",
      "loopScope": "list",
      "windowOpacity": 0.92,
      "entries": [
        { "entryId": 1, "path": "/abs/path/track1.wav" },
        { "entryId": 2, "path": "/abs/path/track2.flac" }
      ]
    }
  ]
}
```

(`inputId` is NOT persisted — it's allocated fresh by the registry on
load, so embedding it in the JSON would just be misleading noise.)

Persisted: `displayName`, `loopScope`, `windowOpacity` (clamped to
[0.5, 1.0] on read), ordered `entries` (entryId is informational only
and discarded on load — the registry's underlying source allocates
fresh handles via `addEntry`; entries are handles, not session
identifiers).

Not persisted: `isPlaying`, `currentEntryId`, `playheadFrames`, the
`missing` flag, the cached `durationFrames`.

Sessions that predate this slice load unchanged (absent `fileInputs`
array → empty).

### 4.8 Error handling matrix

| Situation | Engine behavior | UI behavior |
|---|---|---|
| File missing at load | Entry marked `missing`, no reader opened | Row dimmed, label suffix " — missing" |
| File missing at advance (mid-play) | Entry marked `missing`, worker advances to next valid per loopScope | Row dims live; transport keeps moving |
| All entries missing at load | `isPlaying` forced false; no reader | Player window opens with banner "All files missing"; transport disabled |
| Unreadable file (corrupt header / unsupported codec) | Entry marked `unreadable` (same advance behavior as `missing`) | Row dimmed with " — unreadable" suffix |
| SR mismatch | Transparent resampling | None |
| Channel count > 2 | Take channels 0+1, dev-log warning | None in v1 |
| Mono file (1 ch) | Dual-mono'd at reader | None |
| Duplicate file in playlist | Allowed (deliberate repeats are valid setlist content) | Appears as separate row with own entryId |
| Ring underrun | Audio thread → silence + counter tick | Dev-log only, no operator surface |
| Scrub past EOF | Clamp to last frame; if isPlaying, trigger advance per loopScope | None |
| Strip removed while playing | Worker stops, reader closes, FileInputSource freed | Player window auto-closes |
| Operator picks zero files in picker | No-op | No window opens, no strip added |

### 4.9 UX edge cases (covered by the design above)

- Reorder of the currently-playing track: works (lookup by entryId).
- Empty playlist mid-play (all non-playing removed, then current ends
  with loop=off): transport stops, window stays open with "Playlist
  empty" hint and active `+` button.
- Stop + close window: state retained (paused at current frame); reopen
  shows same state.
- Strip armed for tape recording while file plays: works identically to
  a live mic — audio flows through the channel to tape per the bridge
  slice. This is the testing-enabler purpose.

---

## 5. Test surface

**Headless (Catch2, `IdaTests`):**

- `FileInputDescriptor` round-trip + `PlaylistEntryId` equality.
- `FileInputSource` with a synthesized WAV: register, play, pump
  callback, assert ring delivers expected samples.
- Loop scope: Off + EOF + more entries → advance. Off + EOF + last →
  stop. Track + EOF → same-track seek 0. List + EOF + last → wrap to
  first.
- Missing-file at load: descriptor loads, entry marked missing.
- Missing-file at advance (worker thread): synthesize a session with a
  missing middle entry; play through; assert worker skips it.
- Live list mutation during playback: reorder current track, assert
  reader uninterrupted. Add entry, assert it lands as a successor.
  Remove non-current entry, assert OK; attempt to remove current,
  assert refusal (engine surface returns false).
- SR-mismatch: 44.1 kHz file on 48 kHz device, assert output sample
  count matches resampled expectation.
- Mono dual-mono: 1-channel file, assert L and R buffers carry the
  same samples.
- Session round-trip: write descriptor, read back, assert equality
  (modulo entryId re-derivation). Covers `windowOpacity` round-trip.
- `windowOpacity` clamp: writing 0.3 / 1.2 to a descriptor reads back
  as 0.5 / 1.0.
- Backward-compat: old session JSON without `fileInputs` array loads
  with no file inputs and no errors. Sessions with `fileInputs`
  entries missing `windowOpacity` load with the default (0.92).

**Operator-verify (added to `continue.md`):**

- Add file input with 1 file → strip + window appear → play → audio
  flows through the strip → meter moves.
- Arm strip for tape recording → play file → stop play → load the
  tape into a phrase → audio plays back from tape.
- Add file input with 3 files → loop scope: list → audio cycles
  through all three and wraps.
- Loop scope: track → audio repeats the current track.
- Drag-reorder the currently-playing track to position 3 → keeps
  playing uninterrupted → on next advance, follows the new position.
- Add a track mid-play → appears in list → reachable via next-advance
  or setCurrent.
- Remove a non-current track mid-play → row vanishes → playback
  continues.
- Attempt to remove the current track → `–` button disabled, tooltip
  shown.
- Close player window → strip remains → right-click strip → Show
  player… → window reopens with live state.
- Multiple file inputs open simultaneously → all play, all mix
  through the input mixer chain.
- Right-click a player window → `Window opacity` → pick a preset →
  window translucency updates immediately. Save / quit / reload →
  opacity restored.
- macOS Window menu → File Players submenu → select one → window
  raises.
- Save session → quit → relaunch → load → strips and file paths
  restored, transport stopped at entry 1.
- Move a file on disk → reload session → row shows " — missing", the
  remaining playable entries still work.

---

## 6. Slice plan (final breakdown in the plan doc)

| # | Task | Verify |
|---|---|---|
| 1 | Whitepaper amendments (§6.6 + §7.2 + glossary) | Diff review |
| 2 | `core/` — `InputKind::FileInput`, `LoopScope`, `PlaylistEntryId`, `FileInputDescriptor` | Unit tests |
| 3 | `audio/` — `FileInputSource` engine: format manager, BufferingAudioReader, resampler, ring, worker, transport command queue | Unit tests with synthesized WAV |
| 4 | `audio/` + `engine/` — playlist semantics: entryId stability across reorder, advance-on-EOF per LoopScope, missing-file skip, live list mutation under mutex | Unit tests covering each scope + edge cases |
| 5 | `engine/` — `InputMixer` wiring: `registerFileInput`, transport surface, channel attachment, audio-callback patch | Unit + audio-callback integration tests |
| 6 | `persistence/` — JSON read/write of `fileInputs` array, missing-file-on-load, backward-compat | Round-trip + load-old-session tests |
| 7 | `ui/` — `FileInputPlayerWindow` (transport bar, scrubber, track list, append/remove/drag-reorder, loop-scope cycle) | Operator-verify |
| 8 | `ui/` — `InputMixerPane` blank-area "Add file input…" + strip recall menu + macOS Window menu | Operator-verify |
| 9 | `continue.md` — operator-verify recipe added | Operator runs it |

Tasks 2–6 are headless-testable (TDD applies per
`superpowers:test-driven-development`). Tasks 7–8 are operator-verified
per established IDA convention. Task 1 lands FIRST per house rules
("whitepaper / design before code, no architectural surprises in
implementation commits").

---

## 7. Memory pins explained

- `[[project_minimal_default_mixers]]` — file inputs are *additive*
  sources, not defaulted. Nothing is seeded at session creation. The
  operator opts in by adding one.
- `[[project_input_source_mono_stereo_rme]]` — file inputs are stereo-
  internal at the strip, matching the RME model. Mono files are dual-
  mono'd at the reader stage.
- `[[feedback_gesture_over_ui_clutter]]` — blank-area menu gesture for
  add; strip right-click for recall; no permanent on-strip buttons for
  file-specific affordances.
- `[[feedback_ios_long_press_pairs_right_click]]` — every right-click
  gesture in this slice pairs with long-press on iOS.
- `[[feedback_sirius_done_right_and_complete]]` — playlist is the
  proper feature; no testing-only stub variant.
- `[[feedback_no_deferral_get_it_done]]` — playlist semantics, drag
  reorder, live edit, missing-file handling are all in v1, not
  deferred to "phase 2."
- `[[project_two_mixers_totally_separate]]` — file inputs live in the
  Input Mixer only; Output Mixer is untouched by this slice.
- `[[project_user_guide_alongside_whitepaper]]` — operator how-to for
  this feature is deferred to whenever the user guide doc is born.
