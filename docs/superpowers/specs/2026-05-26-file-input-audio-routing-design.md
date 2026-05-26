# Design — File-input audio routing (engine wiring)

**Date:** 2026-05-26
**Status:** Approved — ready for implementation plan
**Predecessor slice:** `docs/superpowers/specs/2026-05-25-file-input-design.md` (UI + registry + worker shipped; audio-routing follow-on deferred)

---

## Problem

The file-input UI slice (closed at commit `aa67fcd`) ships the full operator surface — `Add file input…` gesture, `FileInputPlayerWindow` with transport + playlist, persistence — and the engine surface — `FileInputSource` (per-input disk-reader + worker + SPSC ring) and `FileInputRegistry` (id allocator + descriptor store + transport facade).

What is **not** wired: `AudioCallback` calls `inputMixer_->renderInputGraph(deviceIn, ...)` per buffer, but `InputMixer` never consults `FileInputRegistry`. File-input audio is produced into ring buffers by the worker thread and never read back out, so it never reaches a strip, never reaches a tape, never reaches the speakers.

This slice closes that gap.

---

## Goal

A channel whose source is a file input renders its file audio through the existing strip → graph → OutputMixer pipeline, audible on the speakers, visible on the strip meter, captured to its tape (when tape-bearing), exactly like a device-input channel.

No change to the operator-visible surface — the player window, playlist, and transport remain the same. The user-visible effect is "press ▶ on the player window → you hear the file."

---

## Non-goals

- **Dynamic add/remove of file inputs during a running audio callback.** The existing `rebuildInputStrips()` bracket pattern (`removeAudioCallback / addAudioCallback` around the mutation) is reused. File-input register/unregister mutations bracket the same way. No lock-free hot-swap.
- **Multi-channel binding** (one file input feeding multiple mixer channels). Works mechanically (the resolved callable is callable from any number of channels) but not specifically validated by tests in this slice.
- **Direct-monitor / sub-ms latency path for file inputs.** File inputs reach the speakers through the normal OutputMixer routing (V9 Slice 3 MON-channel auto-creation). Sub-ms direct monitoring is a separate concept (whitepaper §6.1) that does not apply to non-live sources.
- **Re-touching the file-input UI** — that slice is closed.
- **Plugin scanning, OTTO routing, or any other queued mixer slice.** Strict scope.

---

## Architecture

### Layering (preserved)

- `core/` — JUCE-free pure C++. **Gains:** one new 1-file interface header.
- `audio/` — JUCE audio (juce_audio_basics + juce_audio_formats). **Gains:** `FileInputRegistry` implements the new interface; `FileInputSource` gains a static thunk.
- `engine/` — JUCE-light (juce_core PRIVATE; public headers JUCE-free). **Gains:** forward-declares the interface, holds a pointer, gains one setter; channel state gains an optional callable; `renderInputGraph` branches per channel.
- `app/` — `MainComponent` wires the registry pointer once and binds the callable per file-input strip.

No new transitive JUCE dependency surfaces in `engine/` (the resolved callable is a `void* + function pointer`, JUCE-free).

### The JUCE-free seam

```cpp
// core/include/ida/IFileInputSourceRegistry.h
namespace ida {

/// A resolved pull function for one file-input source. The pull function
/// is RT-safe (noexcept, no allocation, no locks, no I/O); returns true on
/// success or false when the source has no data to provide (caller fills
/// the destination with silence). The userdata pointer remains valid until
/// the source is unregistered. Engine resolves these on the message thread
/// and caches them in channel state; the audio thread invokes the cached
/// pair directly with zero map lookups.
struct FileInputPullCallable
{
    using Fn = bool (*)(void* userdata, float* L, float* R, int numFrames) noexcept;
    Fn    fn       { nullptr };
    void* userdata { nullptr };

    bool valid() const noexcept { return fn != nullptr; }
};

class IFileInputSourceRegistry
{
public:
    virtual ~IFileInputSourceRegistry() = default;

    /// Resolves a pull callable for the source registered under `id`.
    /// Returns an invalid callable (fn == nullptr) when `id` is unknown.
    /// Message thread only. The returned callable's userdata is owned by
    /// the registry; unregistering the source must bracket with audio-
    /// callback removal (the engine never invalidates the cached pair on
    /// its own).
    virtual FileInputPullCallable resolveFileInputPull (InputId id) noexcept = 0;
};

} // namespace ida
```

One method. No abstract-base ceremony. JUCE-free. The original "Path Z layering ceremony" objection from `continue.md` §2 dissolves at this size.

### Data flow (audio thread, per buffer)

```
AudioCallback
  └─ inputMixer_->renderInputGraph(deviceIn, numDeviceChannels, nullptr, 0, numSamples)
       └─ for each channel:
            if (channel.filePull.valid())
                channel.filePull.fn(channel.filePull.userdata, scratchL, scratchR, numSamples)
                    └─ FileInputSource::pullIntoStatic (thunk)
                         └─ FileInputSource::pullInto (existing SPSC-ring consumer)
            else
                memcpy/dual-mono from deviceIn[channel.leftIdx / rightIdx]
            ChannelStrip<Audio>::process (unchanged)
            graph routing (unchanged)
            metering (unchanged)
```

OutputMixer's existing MON-channel auto-creation (V9 Slice 3) reads the channel's post-strip buffer and delivers it to the speakers — same path device-input audio already uses today. No OutputMixer change in this slice.

---

## Components

### 1. `core/include/ida/IFileInputSourceRegistry.h` (NEW)

Header-only, ~40 lines including doc-comments. The interface + `FileInputPullCallable` struct as shown above. Includes `Channel.h` (already in core) for `InputId`. No other deps.

### 2. `core/include/ida/Channel.h` (EDIT — minor)

Add a public constant for the file-input id base, so `engine/`, `audio/`, and tests all reference the same value:

```cpp
/// File-input InputIds start at this value. Below = device-input ids.
/// Lets engine cheaply distinguish file inputs without a registry lookup
/// (the actual binding still goes through setChannelFileInputSource).
static constexpr InputId kFileInputIdBase { 100000 };
```

Replaces the magic literal currently in `FileInputRegistry::nextFileInputId_ { 100000 }` (now initialized from `kFileInputIdBase`).

### 3. `audio/include/ida/FileInputRegistry.h` + `audio/src/FileInputRegistry.cpp` (EDIT)

- Inherit publicly from `ida::IFileInputSourceRegistry`.
- Override `resolveFileInputPull(InputId) noexcept`. Implementation: find the source via the existing private `source_(id)` helper, return `{&FileInputSource::pullIntoStatic, source_ptr}` if found, else default-constructed (invalid) callable.
- Initialize `nextFileInputId_` from `ida::kFileInputIdBase` (no behaviour change).

### 4. `audio/include/ida/FileInputSource.h` + `audio/src/FileInputSource.cpp` (EDIT)

Add a `static` thunk matching `FileInputPullCallable::Fn`:

```cpp
static bool pullIntoStatic (void* userdata, float* L, float* R, int numFrames) noexcept;
```

Implementation casts `userdata` back to `FileInputSource*` and forwards to a JUCE-free overload of `pullInto`. The current `pullInto(juce::AudioBuffer<float>&, int)` stays for existing callers/tests; add an overload `pullInto(float* L, float* R, int numFrames) noexcept` that does the same SPSC ring consumption against raw pointers. The static thunk calls the new overload.

### 5. `engine/include/ida/InputMixer.h` + `engine/src/InputMixer.cpp` (EDIT)

Public surface additions (forward-declare `class IFileInputSourceRegistry; struct FileInputPullCallable;`):

```cpp
/// Message-thread setter. The pointer is stored and used to resolve
/// per-channel file-input callables via setChannelFileInputSource. Pass
/// nullptr to disable file-input routing entirely (the per-channel
/// branch then short-circuits on filePull.valid()).
void setFileInputSourceRegistry (IFileInputSourceRegistry*) noexcept;

/// Message-thread binding. Resolves the file-input pull callable for the
/// given InputId via the registry set by setFileInputSourceRegistry and
/// caches it on the channel's state. The channel must have been created
/// with addChannel(InputId source = <same id>, …) or this is a no-op.
/// Replaces the device-channel path for this channel — calling
/// setChannelInputSource on a file-input channel is a programming error
/// (asserted in debug; ignored in release).
void setChannelFileInputSource (ChannelId, InputId) noexcept;
```

Private state additions:
- One pointer field: `IFileInputSourceRegistry* fileInputRegistry_ { nullptr };`
- One per-channel field on the existing `InputState` (or equivalent channel record): `FileInputPullCallable filePull;` (zero-initialized).

`renderInputGraph` change: in the per-channel source-gather block, branch on `channel.filePull.valid()`. If true, invoke the callable into the existing pre-strip stereo scratch; if it returns false, zero-fill the scratch (silent for that block). Else, existing device-channel gather.

No other behaviour change. Metering, routing, tape delivery, graph evaluation — all unchanged.

### 6. `audio/src/AudioCallback.cpp` (NO CHANGE)

The render-graph call signature is preserved (`renderInputGraph(deviceIn, n, nullptr, 0, numSamples)`). AudioCallback stays oblivious to file inputs.

### 7. `app/MainComponent.cpp` (EDIT)

Two surgical additions:

- During engine setup (next to where `inputMixer_` is constructed and wired): one-line `inputMixer_->setFileInputSourceRegistry(&fileInputRegistry_);`
- In the existing strip-rebuild path that maps file-input strips: when a strip is for a file input, call `inputMixer_->setChannelFileInputSource(channelId, inputId);` instead of `setChannelInputSource(channelId, leftDev, rightDev, stereo);`
- `FileInputRegistry::registerFileInput` and `unregisterFileInput` call sites must run **inside** the existing `removeAudioCallback / addAudioCallback` bracket. Inspect the current call sites and add the bracket if missing.

---

## RT-safety contract

Per the audio-thread rules (`docs/RT_SAFETY_CONTRACT.md`):

- `FileInputPullCallable::fn` is `noexcept` by signature. The thunk's implementation calls `FileInputSource::pullInto(float*, float*, int) noexcept`, which is an SPSC-ring consumer already shipped + tested in the file-input slice. No alloc, no locks, no I/O.
- The cached callable in channel state is a POD (two pointers). Reading it on the audio thread is wait-free.
- The registry itself is never touched on the audio thread. Resolution happens once on the message thread; the callable is cached.
- Userdata-invalidation safety: `FileInputRegistry::unregisterFileInput` invalidates a callable's userdata (the `FileInputSource` is destroyed). Therefore `unregisterFileInput` MUST run inside the `removeAudioCallback / addAudioCallback` bracket (the existing pattern used by `rebuildInputStrips()` for input-source mutations). This is enforced by inspection at the call site, not by the type system.

---

## Testing

New headless test file: `tests/InputMixerFileInputTests.cpp` (~6 cases, ~150 assertions).

Stub `IFileInputSourceRegistry` whose `resolveFileInputPull` returns a callable whose `fn` fills L/R with a known pattern (e.g. a ramp 0.0 → 1.0). The stub lets the test exercise the entire `renderInputGraph` file-input branch deterministically without `juce_audio_formats` or any disk reader.

Cases:
1. **Bind + render** — register a file-input InputId on the stub, add a channel, bind via `setChannelFileInputSource`, run `renderInputGraph` with empty `deviceIn`, assert post-strip buffer matches the stub's pattern through the channel's unity-gain strip.
2. **No registry** — channel created with a file-input InputId but no `setFileInputSourceRegistry` call → `filePull` stays invalid → post-strip buffer is silent (graceful no-op).
3. **Unknown InputId** — registry returns invalid callable for an id it doesn't know → channel renders silence.
4. **Device + file mixed** — one channel device-input (sources from `deviceIn[0]`), one channel file-input (sources from stub). Both render correctly into their respective post-strip buffers in one `renderInputGraph` call.
5. **Multi-call stability** — three consecutive `renderInputGraph` calls with the same bound file-input channel produce three frames of the stub's pattern (consumer state on the stub side advances correctly).
6. **Pull returns false** — stub returns false → channel renders silence for that block; subsequent block where stub returns true renders the pattern.

Also extend `tests/FileInputRegistryTests.cpp` (3 → 4 cases): one case verifying `resolveFileInputPull` returns valid for registered ids and invalid for unknown ids.

No GUI / operator-eyes-on test (engine-only slice). Run the full `ctest` suite + the `[file-input]` Catch2 tag to confirm no regression.

---

## Verification

```bash
cmake --build build --target IdaTests
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" -j      # expect 762/762 baseline + 6 new
./build/tests/IdaTests "[file-input]"                                # expect 27 → 28 cases + new assertions
./build/tests/IdaTests "[input-mixer]"                               # confirm no regression
```

Then a brief operator-eyes-on (with a real .app build):

1. Launch IDA.
2. `Add file input…` → pick a WAV. Player window opens.
3. Press ▶.
4. **Confirm:** audio is audible on the speakers. The input strip's meter moves. The pill in §1.A step 4 of `continue.md` is now a real check, not a deferred-audio note.
5. Add a second file input. Both play simultaneously, both meters move.
6. Save the session, quit, relaunch, load. Press ▶ on each — both still play.

---

## Critical files (only files touched)

NEW:
- `core/include/ida/IFileInputSourceRegistry.h`
- `tests/InputMixerFileInputTests.cpp`

EDIT:
- `core/include/ida/Channel.h` (add `kFileInputIdBase` constant)
- `audio/include/ida/FileInputRegistry.h` (inherit interface; declare override)
- `audio/src/FileInputRegistry.cpp` (implement `resolveFileInputPull`; initialize `nextFileInputId_` from constant)
- `audio/include/ida/FileInputSource.h` (declare static thunk + raw-pointer `pullInto` overload)
- `audio/src/FileInputSource.cpp` (implement thunk + overload)
- `engine/include/ida/InputMixer.h` (forward-declare interface; add two setters; add `FileInputPullCallable` to channel state)
- `engine/src/InputMixer.cpp` (implement setters; branch in `renderInputGraph`)
- `app/MainComponent.cpp` (wire registry pointer; call `setChannelFileInputSource` for file-input strips; verify `register/unregister` bracket the audio callback)
- `tests/FileInputRegistryTests.cpp` (one new case for `resolveFileInputPull`)

No changes to: spec docs other than this one, whitepaper, OTTO submodule, `persistence/`, `host/`, `ui/`, `app/MainComponent.cpp` UI surfaces.

---

## Why not the other paths

- **Path Y (pre-mix in AudioCallback).** AudioCallback would have to synthesise virtual channel columns and know the channel → InputId mapping that currently lives in InputMixer. Two new responsibilities for a layer that's currently a 100-line pass-through. Harder to test (renders look like device inputs). Rejected.
- **Original Path X (extend `renderInputGraph` to take `FileInputRegistry*`).** Forces `engine/src/InputMixer.cpp` to `#include "ida/FileInputRegistry.h"`, which transitively includes `juce_audio_formats` — meaningful violation of the engine layering rule (`juce_core` is the only JUCE in engine's PRIVATE deps today). Avoidable. Rejected.
- **Original Path Z (abstract base in engine).** Spirit accepted, scale reduced. The current design IS Path Z, but the interface is one method and the only ceremony is a single virtual call. The original objection ("layering ceremony") doesn't apply at this size.

---

## Cross-platform

Pure C++. The only platform-touching code on the path (`FileInputSource` decoding WAV/AIFF/FLAC via `juce_audio_formats`) already shipped in the predecessor slice and runs on all four IDA targets (macOS, iOS, Windows, Linux). This slice adds no platform-specific surface.
