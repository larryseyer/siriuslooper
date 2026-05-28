# OTTO Transport Bar (Option B) + Asset Path Injection — Design Spec

Status: BS-5 output of the 2026-05-28 brainstorm. Supersedes §2.3 of
`docs/superpowers/specs/2026-05-27-otto-integration-architecture.md` (TransportBar
component definition) and locks the asset-path recipe from `continue.md` §3
(2026-05-27 session). Pre-decided what was previously listed as
`§6 open item 2` (TransportBar placement) and `§6 open item 6` (asset bundling
dev-loop side).

This spec is the BS-5 input to operator review (BS-6), which is the precursor
to `superpowers:writing-plans` (BS-7). Implementation lands as two sequenced
slices, S3a (transport bar) then S3b (asset path), per §7 below.

The doctrinal anchor remains whitepaper V10 §5.7 (OTTO as bundled rhythm
engine and tempo-map source). The 2026-05-27 integration spec's §1.2
commitments stand verbatim — this spec refines §2.3's component shape from
"build a new IDA-side TransportBar widget" (option A) to "IDA owns its own
`otto::ui::TransportBar` instance" (option B, operator-clarified 2026-05-28).

---

## Context

S2 of the 2026-05-27 integration plan landed `ida::OttoPane` (the OTTO
top-level tab embedding `OTTOProcessor::createEditor()`) as IDA atomic commit
`af2d947`. Operator-verified GUI launch surfaced two issues that this spec
resolves:

1. **Transport bar location was wrong.** The 2026-05-27 spec §2.3 framed
   the persistent IDA-wide transport bar as option A: build a new
   IDA-side `TransportBar` widget that subscribes to OTTO's transport
   events and routes control commands back. The operator clarified the
   actual intent at S2 verification:
   > *"OTTO's Top Bar is supposed to be IDA's top bar... we only need ONE
   > transport bar... the transport should be visible AT ALL TIMES regardless
   > of which tab is selected... It should sit ABOVE the tabs."*

   That is option B: reuse OTTO's existing `otto::ui::TransportBar`
   Component class, mount one instance above IDA's tab strip. The S2
   work-in-progress that OR'd `setEmbeddedInHost` into `isPluginMode_` to
   hide OTTO's transport (OTTO commit `fb5ff039`) was reverted mid-session
   (OTTO commit `f2b6f6db`) under an option-A reading; this spec re-applies
   the OR (effectively reverting the revert) because option B mounts IDA's
   bar above the tabs and DOES want OTTO's tab-internal bar hidden.

2. **OTTO sample assets aren't loading.** Every OTTO player falls back to
   synth-mode because `otto::library::SamplerPresetLoader::findSamplerFolder()`
   probes paths off `juce::File::currentExecutableFile`, which is
   `IDA.app/Contents/MacOS/IDA` — not `OTTO.app`. The same problem affects
   the IR loader and `otto::paths::PresetPaths::getRoot(StorageTier::Factory)`.

Both topics fit naturally in a single design pass: they're the loose ends
left by S2 verification, and they ship in two independent slices that
don't share state.

---

## 1. Architecture

### 1.1 Window shape

```
                 ┌──────────────────────────────────────────────────────┐
                 │ MainComponent                                         │
                 │                                                       │
                 │ ┌──────────────────────────────────────────────────┐ │
                 │ │ idaTransportBarHost_   (NEW — sibling of tabs_)  │ │
                 │ │   ┌────────────────────────────────────────────┐ │ │
                 │ │   │ otto::ui::TransportBar (IDA-owned)         │ │ │
                 │ │   │  ▶  120.0  4/4 · 1.1  [══ MASTER ══════]   │ │ │
                 │ │   └────────────────────────────────────────────┘ │ │
                 │ └──────────────────────────────────────────────────┘ │
                 │ ┌──────────────────────────────────────────────────┐ │
                 │ │ tabs_  [Perf][Prep][In Mix][Out Mix][OTTO][Tapes]│ │
                 │ │ ┌─────────────────────────────────────────────┐  │ │
                 │ │ │ active tab content                           │  │ │
                 │ │ │   (when OTTO tab: OTTOEditor with its own    │  │ │
                 │ │ │    transportBar_ HIDDEN via isPluginMode_)   │  │ │
                 │ │ └─────────────────────────────────────────────┘  │ │
                 │ └──────────────────────────────────────────────────┘ │
                 └──────────────────────────────────────────────────────┘
```

`MainComponent::resized()` reserves a top strip for `idaTransportBarHost_`,
then lays out `tabs_` in the remaining bounds. The top strip's height is
derived from `otto::ui::TransportBar`'s internal Breakpoint logic — the
bar already adapts (Phone / Tablet / Desktop) and IDA gives it whatever
its preferred height is at the current main-window width.

### 1.2 Architectural commitments

1. **One TransportBar Component class, two instances.** `otto::ui::TransportBar`
   stays a JUCE Component class defined in OTTO. IDA constructs its own
   instance via a wrapper (`ida::TransportBarHost`); OTTO's `OTTOEditor`
   continues to construct its own instance internally. Only IDA's
   instance is visible — OTTO's is hidden via `isPluginMode_` when
   `setEmbeddedInHost(true)`.

2. **OTTO is the transport source (V10 §5.7).** Play/Stop/setTempo/tapTempo
   from IDA's bar route to OTTO. The bar reflects OTTO's actual transport
   state via the existing `IOttoTransportListener` round-trip — not
   commanded state.

3. **Meter + spectrum reflect IDA's master output, not OTTO's.** Pro-audio
   convention: a transport bar's meter shows what's leaving the speakers.
   Requires IDA to publish master meter + master spectrum (neither exists
   today — both are part of this spec's S3a slice, per the "no half-baked"
   rule).

4. **Asset path injection is OTTO-side architecture, not IDA monkey-patch.**
   A new `otto::paths::AssetsRoot` singleton owns the override mechanism.
   IDA calls `setOverride` once at session init. The 3 OTTO call sites
   refactor to consult `AssetsRoot` first, falling through to their
   existing per-platform ladders. OTTO standalone behaviour byte-identical.

5. **Installer-time asset copy stays out of scope.** Layer C of the §3
   recipe (copy OTTO assets into `IDA.app/Contents/Resources/` at install
   time) is deferred. Dev-loop fix (Layer A + B) is what this spec lands.
   Production binaries get their override path later, without touching
   OTTO source.

### 1.3 What stays unchanged

- The 2026-05-27 integration spec's §1.2 five commitments (OTTO is part of
  IDA, OttoHost embeds OTTOProcessor, OTTO's UI is a top-level IDA tab, a
  persistent IDA-wide transport bar drives OTTO's transport, OTTO does not
  host third-party plugins inside IDA).
- The S1 + S2 atomic commits (af2d947 and predecessors). S1's
  OTTOProcessor embed, S2's OttoPane, the `setEmbeddedInHost` flag pair on
  OTTOProcessor, and the PreferencesDialog `bool embedded` gate are all
  load-bearing for this spec.
- The cross-project inbox protocol. OTTO-side edits in S3a (re-revert) +
  S3b (`AssetsRoot`) flow through the inbox + `Ida-Origin:` trailer cycle.
- OTTO standalone build behaviour. The `AssetsRoot` refactor's fallback
  rule guarantees byte-identical behaviour when no override is set.

---

## 2. Components

### 2.1 `ida::TransportBarHost` (NEW — `app/`)

**Responsibility.** Owns the visible `otto::ui::TransportBar` instance.
Implements `otto::ui::TransportBarListener` (events from the bar →
OttoHost). Implements `ida::IOttoTransportListener` (OTTO state →
bar.setTransportState/setTempo). Runs a 30 Hz `juce::Timer` that pushes
IDA-master meter + spectrum data into the bar.

**Where.** `app/TransportBarHost.h` + `app/TransportBarHost.cpp` (new).

**Ownership / lifetime.** Constructed by `MainComponent` at session init,
declared **after** `ottoHost_` so destruction order is safe (TransportBarHost
holds a ref to OttoHost). Destroyed at session teardown. Holds the
TransportBar as a value member (composed, not unique_ptr).

**Public API surface.**
```cpp
class TransportBarHost : public juce::Component,
                         public otto::ui::TransportBarListener,
                         public ida::IOttoTransportListener {
public:
    explicit TransportBarHost(OttoHost& host);
    ~TransportBarHost() override;

    // juce::Component
    void resized() override;

    // Test seam — drive the bar headlessly
    otto::ui::TransportBar& getBar() noexcept;
};
```

**RT-safety obligations.** Message-thread only. The 30Hz timer pulls from
atomic snapshots published by the IDA-master-meter / IDA-master-spectrum
publishers (defined below).

### 2.2 `ida::OttoHost` extensions (EVOLVED — `otto-bridge/`)

**What's added.** Transport-control accessors + master-snapshot accessor.

```cpp
// New transport-control accessors (message-thread)
void play();
void stop();
void setTempo(double bpm);
void tapTempo();

// New audio-snapshot accessor (read-side; std::atomic-backed)
struct MasterSnapshot { float leftDb; float rightDb; float peakDb; float lufs; };
MasterSnapshot snapshotMaster() const noexcept;
```

`play`/`stop`/`setTempo`/`tapTempo` forward to OTTOProcessor's existing
transport actions on the message thread. `snapshotMaster` returns the
latest published master-meter snapshot (NOT OTTO's master — IDA's
post-OutputMixer master).

**Existing surface preserved unchanged.** `renderBlock`, 32-output accessors,
`addTransportListener`, `getProcessor`, `serializeState`, `restoreState`.

**Threading.** All new methods are message-thread except `snapshotMaster`,
which is callable from any thread (atomic load).

### 2.3 IDA Master Meter Publisher (NEW — `audio/` or `engine/`)

**Responsibility.** Publishes the IDA master output's peak / RMS / LUFS
per audio block as a lock-free atomic snapshot.

**Where.** Inside `OutputMixer` (or the post-mixer master section), at
the existing master-bus computation point. Exact location TBD at S3a
slice-design time after auditing the OutputMixer master code path.

**Threading.** Audio-thread publisher (writes to `std::atomic<MasterSnapshot>`).
Message-thread reader (`OttoHost::snapshotMaster` loads). Conforms to
`docs/RT_SAFETY_CONTRACT.md`: no allocations, no locks, no I/O.

### 2.4 IDA Master Spectrum Publisher (NEW — `audio/` or `engine/`)

**Responsibility.** Computes a per-bin FFT magnitude (in dB) of the IDA
master output and publishes it as a lock-free atomic snapshot for the
TransportBar's spectrum display.

**Where.** Same architectural neighbourhood as the master meter publisher.
FFT runs at a fixed sample interval (suggest 2048-sample window, hop one
block — exact size TBD at slice-design time matching the bar's
`configureSpectrum(int numBins, double sampleRate)` expectation).

**Threading.** Audio-thread FFT input collection + atomic-buffer publish.
Message-thread bin readout via a new `OttoHost::snapshotSpectrum(int bin)`
or equivalent accessor (exact surface TBD).

**RT-safety.** FFT scratch buffers allocated once in `prepare()`. The
audio-thread path is alloc-free and lock-free.

### 2.5 `MainComponent` (EVOLVED — `app/`)

**Changes.**
- New member: `std::unique_ptr<ida::TransportBarHost> transportBarHost_`,
  declared **after** `ottoHost_` and **after** `ottoPane_` in the member
  list (destruction LIFO: pane → host → ottoHost → unique-ptr-owned
  bar → ok).
- Ctor: `transportBarHost_ = std::make_unique<ida::TransportBarHost>(*ottoHost_)`;
  `addAndMakeVisible(*transportBarHost_)` after `addAndMakeVisible(tabs_)`.
- `resized()`: compute top-strip height from
  `transportBarHost_->getBar().getRequiredHeightForWidth(getWidth())`
  (or the equivalent OTTO breakpoint helper — exact API confirmed at
  slice-design time); `transportBarHost_->setBounds(area.removeFromTop(h))`;
  `tabs_.setBounds(area)` for the rest.

### 2.6 OTTO `PluginEditor.cpp` re-revert (CROSS-PROJECT — `external/OTTO/`)

**Change.** Re-apply the `|| proc.isEmbeddedInHost()` OR in the
`isPluginMode_` initializer of `OTTOEditor`. Effectively reverts OTTO
commit `f2b6f6db`.

**Why this isn't undoing valid history.** `f2b6f6db` reverted because
the prior design (option A) left no visible transport surface inside
the OTTO tab. Option B mounts IDA's bar **above** the tabs, visible
everywhere — OTTO's tab body needs no internal transport. The OR is now
the correct direction.

**Inbox protocol.** Cross-project commit with `Ida-Origin:` trailer +
new `[FROM IDA → OTTO]` entry. The 2026-05-27 entry that documents the
original OR (OTTO commit `fb5ff039`) plus the entry that documents
`f2b6f6db`'s revert are both still in the OTTO inbox at session start;
S3a's new entry chains the design rationale.

### 2.7 `otto::paths::AssetsRoot` (NEW — OTTO-side, `external/OTTO/`)

**Responsibility.** Single-instance authority on where OTTO's runtime
assets live. Honour an override when one is set; otherwise fall through
to existing per-platform ladders.

**Where.** `external/OTTO/src/otto-core/include/otto/paths/AssetsRoot.h`
(header-only or paired `.cpp` — slice-design choice).

**Public API.**
```cpp
namespace otto::paths {

class AssetsRoot {
public:
    static AssetsRoot& instance();

    // Call ONCE at session init, message-thread, before prepareToPlay.
    void setOverride(juce::File root);

    // Returns: override if set, else search via existing per-platform
    // ladder, else empty juce::File{} → caller falls back to its old
    // code path (defensive — shouldn't happen in practice).
    juce::File get() const;
    juce::File samplerFolder() const;
    juce::File irFolder() const;
    juce::File factoryPresetsFolder() const;

private:
    std::optional<juce::File> override_;
    mutable std::mutex setMutex_;  // guards setOverride; reads are
                                   // single-publisher-after-init so
                                   // direct read of optional is safe.
};

} // namespace otto::paths
```

**Threading.** `setOverride` is message-thread, called once at init
before any other thread starts touching it. After init, reads are
unsynchronised — the contract is "set once, then read many."

**Refactor at the 3 call sites.** Original ladder logic moves inside
`AssetsRoot::samplerFolder()` (etc.) as the fallback. OTTO standalone's
behaviour is unchanged because the override is just unset.

### 2.8 IDA `OttoHost::Impl::Impl()` change (EVOLVED — `otto-bridge/`)

**Change.** Add one call at the very top of the ctor body, before
`processor_` is constructed:
```cpp
otto::paths::AssetsRoot::instance().setOverride(juce::File{ IDA_OTTO_ASSETS_DIR });
processor_ = std::make_unique<OTTOProcessor>();
processor_->setEmbeddedInHost(true);
// ... rest of existing ctor
```

`IDA_OTTO_ASSETS_DIR` is a new compile-def (added in
`otto-bridge/CMakeLists.txt`) sourced from the existing top-level
`OTTO_ASSETS_DIR` CMake cache variable (defined in `CMakeLists.txt:22`,
defaults to `/Users/larryseyer/AudioDevelopment/OTTO/assets`).
Production binaries override `OTTO_ASSETS_DIR` via CMake variable.

---

## 3. Data flow

### 3.1 Transport: user → OTTO (message-thread)

```
user clicks idaTransportBar_'s Play button
  → otto::ui::TransportBar fires playPauseClicked() to its listener
    → ida::TransportBarHost::playPauseClicked()
      → ottoHost_.play() or ottoHost_.stop() depending on current state
        → OTTOProcessor's transport-start path
          → TransportTracker emits TransportEvent::Started
            (data flow §3.2 propagates back)
```

### 3.2 Transport: OTTO → bar (audio-thread → message-thread)

```
OTTOProcessor::processBlock (audio-thread)
  → TransportTracker::update detects state change
    → EventBus<TransportEvent>::publish
      (currently allocates — separate OTTO-side fix pending in inbox)
      → OttoHost subscription callback
        → marshal payload into audio→message SPSC ring
OttoHost timer drainer (message-thread)
  → pop snapshot
    → for each IOttoTransportListener:
        listener->onOttoTransport(snapshot)
        ↓
      ida::TransportBarHost::onOttoTransport
        → idaTransportBar_.setTransportState(snapshot.isPlaying ? Playing : Stopped)
        → idaTransportBar_.setTempo(snapshot.bpm)
        → (boundary-conversion rule applied: bpm double → Rational stored
           internally if any IDA consumer needs musical-time math — TBD
           at slice-design time; for display, double is fine.)
```

### 3.3 Master meter + spectrum: IDA → bar (30Hz, message-thread)

```
ida::TransportBarHost::Timer::timerCallback (30Hz, message-thread)
  → MasterSnapshot snap = ottoHost_.snapshotMaster();
    → idaTransportBar_.setMasterLevels(snap.leftDb, snap.rightDb);
    → idaTransportBar_.setMasterPeak(snap.peakDb);
    → idaTransportBar_.setMasterLUFS(snap.lufs);
  → for bin = 0 .. N: idaTransportBar_.setSpectrumBin(bin, ottoHost_.spectrumBinDb(bin));
  → bar/beat position: derived from LMC sample count × current BPM
                       (per the 2026-05-27 integration spec §2.3 position
                        derivation rule — IDA computes, doesn't trust
                        OTTO's positionInBeats double)
    → idaTransportBar_.setPosition(bar, beat);
```

### 3.4 Asset path resolution (one-time at init, message-thread)

```
MainComponent::MainComponent()
  → ottoHost_ = std::make_unique<OttoHost>();   // ctor body:
                                                 //
                                                 // otto::paths::AssetsRoot::instance()
                                                 //   .setOverride(IDA_OTTO_ASSETS_DIR);
                                                 //
                                                 // processor_ = make_unique<OTTOProcessor>();
                                                 //   ctor: any path-resolution call now
                                                 //         hits AssetsRoot first.
                                                 //
                                                 // processor_->prepareToPlay(sr, blockSize);
                                                 //   any FlAC/preset/IR load uses the
                                                 //   override path.
```

---

## 4. Error handling

### 4.1 IDA master output not yet publishing meter / spectrum

If S3a's master-meter / master-spectrum publisher slices haven't landed
yet, `OttoHost::snapshotMaster()` returns `{0, 0, -∞, -∞}` (silent
master). The TransportBar renders an empty meter strip — not a bug;
just no signal. The plan must land these publishers as sub-tasks of
S3a; no shipping with the bar but no data.

### 4.2 Double TransportBar visible (regression)

Prevented by the unit test `[otto-pane-no-internal-transport]`: assert
that when `OTTOProcessor::setEmbeddedInHost(true)` is in effect, the
OTTOEditor's internal `transportBar_` is hidden / not in its visible
hierarchy. If the test ever fires, the OTTO-side `isPluginMode_` OR
has regressed and the operator gets a visual double-bar; the test
catches it before commit.

### 4.3 OTTO `AssetsRoot::setOverride` called after init

Defensive: `setOverride` takes the mutex and updates the optional. Any
in-flight `samplerFolder()` etc. read sees either the old or new value
(atomic w.r.t. each call). Calling `setOverride` after init is
discouraged but not catastrophic; in practice IDA only calls it once at
the top of `OttoHost::Impl::Impl()`.

### 4.4 OTTO assets directory missing entirely

If `IDA_OTTO_ASSETS_DIR` points at a non-existent path, `AssetsRoot`
returns that path anyway (it doesn't validate). OTTO's loaders then
fail to find samples — same failure mode as today (synth-mode fallback).
Operator-facing diagnosis is unchanged from today; this spec doesn't
make it worse, and doesn't try to make it better (asset directory
existence is an install-time concern).

### 4.5 OTTO standalone broken by the AssetsRoot refactor

Prevented by the fallback rule: when `override_` is `std::nullopt`,
`AssetsRoot::samplerFolder()` runs the same code that
`SamplerPresetLoader::findSamplerFolder()` runs today, verbatim.
OTTO standalone has zero behavior change.

A Catch2 test `[assets-root][no-override-fallback]` asserts this: with
no override, the returned `samplerFolder()` is byte-identical to the
pre-refactor `findSamplerFolder()` result on a test fixture.

### 4.6 Audio-thread pollution from the new publishers

Prevented by the existing RT-safety contract (`docs/RT_SAFETY_CONTRACT.md`).
The new master-meter + master-spectrum publishers conform: no allocations,
no locks, no I/O, std::atomic snapshot only. The new Catch2 test
`[ida-master-meter][rt-safety]` arms an alloc counter around 10k
publishes and asserts zero allocations.

---

## 5. Testing

### 5.1 Headless (Catch2)

**S3a:**
- `[transport-bar-host][construction]` — TransportBarHost constructs against
  OttoHost; getBar() returns a live TransportBar instance.
- `[transport-bar-host][user-events]` — Synthetic TransportBarListener
  events (playPauseClicked / stopClicked / tempoChanged / tapTempo)
  route to corresponding OttoHost method calls (verified via spy on
  OttoHost or via observable OTTOProcessor state change).
- `[transport-bar-host][otto-events]` — Synthetic IOttoTransportListener
  events (onOttoTransport with isPlaying / bpm) update the bar's
  observable state (getTransportState / getTempo).
- `[ida-master-meter][publish]` — Master-meter publisher's atomic
  snapshot reflects the input audio's peak/RMS/LUFS after a block.
- `[ida-master-meter][rt-safety]` — 10k publishes, zero allocations.
- `[ida-master-spectrum][publish]` — Spectrum bin readout reflects a
  known-frequency sine input.
- `[ida-master-spectrum][rt-safety]` — Same alloc-count guarantee.
- `[otto-pane-no-internal-transport]` — Construct OttoPane against an
  OTTOProcessor with `setEmbeddedInHost(true)`; assert OTTOEditor's
  internal `transportBar_` is not in its visible hierarchy
  (`isShowing()` / `getParentComponent()` chain check).

**S3b:**
- `[assets-root][set-override]` — `AssetsRoot::setOverride(X)` →
  `get()` returns `X`; `samplerFolder()` returns `X / "Sampler"`; etc.
- `[assets-root][no-override-fallback]` — With no override, all four
  accessors return what the pre-refactor logic would have returned on
  a synthetic platform fixture.
- `[assets-root][concurrent-reads]` — After init, parallel reads from
  N threads observe a consistent result (smoke test).

**Baselines preserved:**
- `[otto-host-render]` — 6 cases / 157 assertions.
- `[otto-host-transport]` — 6 cases / 30 assertions.
- `[otto-host-processor-access]` — 3 cases (S2's processor-embed regression
  pins).
- `[otto-pane-construction]` — 2 cases.

### 5.2 Operator-verified (GUI)

**S3a (after landing):**
1. Launch `IDA.app`.
2. Verify the TransportBar is visible at the top of the window.
3. Switch tabs (Performance / Preparation / In Mix / Out Mix / OTTO /
   Tapes / Plugins / Video / Settings). Verify the bar stays visible
   on every tab and meter is responsive.
4. Click the bar's Play button. Verify audio is audible through the
   master (M-OTTO-4 audibility regression check).
5. Switch to a non-OTTO tab during playback. Verify the bar's Stop button
   still stops OTTO.
6. Verify the OTTO tab no longer shows OTTO's internal transport row
   (the player rack starts immediately below the tab strip).
7. Tap the tempo button repeatedly. Verify BPM updates in the bar AND
   inside OTTO's UI (sync round-trip).

**S3b (after landing):**
1. Launch `IDA.app`.
2. Open OTTO tab. Open kit picker on any player.
3. Verify sample-based kits (LSAD pop, LSAD rock, percs, shakers, hands)
   load and play samples — no synth-mode fallback.
4. Verify factory presets load and route through OTTO's pattern playback.

### 5.3 Whitepaper conformance

Unchanged from the 2026-05-27 integration spec's §5.3. OTTO is the
transport source; LMC discipline hierarchy unchanged; OTTO not framed
as a plugin in any new code or doc.

---

## 6. Open items (resolve at slice-design time)

1. **Exact location of the IDA master-meter + master-spectrum publishers.**
   Inside `OutputMixer`'s master section, or in a separate
   `audio/MasterPublisher.{h,cpp}` unit? Decided at S3a's writing-plans
   pass after auditing OutputMixer's master code path. Either is fine —
   the seam is the atomic snapshot, not the file layout.

2. **Spectrum FFT window size / hop / bin count.** OTTO's TransportBar
   internally calls `configureSpectrum(int numBins, double sampleRate)` —
   IDA matches whatever OTTO's bar expects. Suggest 2048-sample window,
   per-block hop. Confirmed at S3a slice-design time.

3. **Bar height policy.** OTTO's TransportBar internally adapts via
   `BreakpointListener` (Phone / Tablet / Desktop). IDA delegates: the
   bar tells us its preferred height for the current window width.
   Slice-design pass confirms the exact accessor (likely a new
   `getPreferredHeight()` helper added OTTO-side, cross-project edit).
   If the helper doesn't exist, default to 88px Desktop / 64px Tablet /
   48px Phone hardcoded in `MainComponent::resized()`.

4. **Position derivation timing.** §3.3 says "LMC sample count × Rational
   BPM at the segment that contains the current sample." Position
   updates fire from the 30Hz timer using `Lmc::sampleCount()`. Boundary-
   conversion rule (double→Rational) applied at the LMC integration
   point. Confirmed at S3a slice-design time.

5. **OTTO inbox SHA backfill (housekeeping).** continue.md §4 notes the
   2 OTTO commits this session (`fb5ff039`, `f2b6f6db`) have
   `Ida-Origin: <pending>` trailers. Optional cleanup; not blocking.

---

## 7. The two slices (input to `superpowers:writing-plans`)

These are the operator-and-Claude sketch; `superpowers:writing-plans`
(BS-7) will produce the rigorous task-by-task plan with verification
criteria after this spec is approved.

| Slice | Goal | Size | Operator-visible? |
|---|---|---|---|
| **S3a** | Persistent IDA-wide TransportBar (option B). IDA owns its own `otto::ui::TransportBar` instance + `TransportBarHost` wrapper. IDA master-meter + master-spectrum publishers. OTTO's tab-internal bar hidden via re-applied `isPluginMode_` OR. | medium-large | **yes — bar visible everywhere; clean OTTO tab; audible OTTO** |
| **S3b** | OTTO asset-path injection. `otto::paths::AssetsRoot` singleton + 3 OTTO call-site refactors. IDA `setOverride` call + `IDA_OTTO_ASSETS_DIR` CMake compile-def. | small-medium | **yes — sample-based kits load** |

**Sequence: S3a first, S3b second.** S3a is the bigger operator-visible
win (the transport surface). S3b is the OTTO-kits-load win. They're
independent — neither blocks the other — but S3a is more visually
satisfying first.

---

## 8. Cross-project considerations

- **S3a cross-project edits:** (a) re-revert of `f2b6f6db` re-applying the
  `isPluginMode_` OR; (b) possible new OTTO-side helper
  `TransportBar::getPreferredHeight(int width)` if it doesn't exist;
  (c) the master-meter / master-spectrum publishers are IDA-side, not
  cross-project.
- **S3b cross-project edits:** all OTTO-side. New `otto::paths::AssetsRoot`
  header + refactor of `SamplerPresetLoader::findSamplerFolder`,
  `IR loader`, and `PresetPaths::getRoot(StorageTier::Factory)`.
- **Inbox protocol applied to both slices.** Each cross-project commit
  carries an `Ida-Origin:` trailer + a new entry in
  `external/OTTO/CROSS_PROJECT_INBOX.md` describing the change and the
  expected OTTO-side ack.
- **OTTO submodule SHA bumps.** Two bumps total across the design: one
  at the end of S3a (incorporating the re-revert + any helper), one at
  the end of S3b (incorporating the AssetsRoot).

---

## 9. Conformance check against V10 §5.7 and the 2026-05-27 integration spec

| V10 §5.7 / 2026-05-27 spec commitment | This spec's realization |
|---|---|
| OTTO is part of IDA, not a plugin | §1.2 commitment 1; reaffirmed |
| Persistent IDA-wide transport bar drives OTTO's transport | §1.1 diagram; §2.1; §3.1–3.2 |
| OTTO is the transport source | §1.2 commitment 2; §3.1 |
| Boundary-conversion rule (bpm double → Rational at receipt) | §3.2 noted; specific helper deferred to slice-design |
| Position derivation from LMC, not OTTO's positionInBeats | §3.3; §6 item 4 |
| OTTO's UI is a top-level tab; OTTO's internal transport hidden | §1.2 commitment 1; §2.6 re-revert |
| OTTO does not host third-party plugins inside IDA | unchanged — outside this spec's scope |
| OTTO's presets unified (S4) | unchanged — separate slice in 2026-05-27 integration |
| Meter shows what's leaving the speakers | §1.2 commitment 3 (new — pro-audio default) |
| AssetsRoot mechanism preserves OTTO standalone behaviour | §1.2 commitment 4; §2.7 fallback rule; §4.5 test |

---

## 10. Self-review checklist (done at write time)

- [x] Every operator-clarified design decision from continue.md §2 and §3
      shows up in the spec.
- [x] References the 2026-05-27 integration spec §2.3 as the predecessor
      and identifies what it got wrong (option A vs B framing).
- [x] References whitepaper V10 §5.7 as doctrinal anchor.
- [x] Five sections present (Architecture §1, Components §2, Data flow §3,
      Error handling §4, Testing §5).
- [x] Open items called out explicitly (§6) rather than pre-decided.
- [x] Two slices enumerated (§7) with sequencing rationale.
- [x] Cross-project considerations (§8) call out inbox + Ida-Origin
      trailer + SHA bump cycle for each cross-project commit.
- [x] No "TODO" / "FIXME" / "TBD" left undocumented — every
      "confirmed at slice-design time" is bounded.

---

*Spec authored 2026-05-28 as the BS-5 output of the OTTO transport-bar
and asset-path brainstorm. Next: operator review (BS-6), then
`superpowers:writing-plans` (BS-7) to produce the rigorous slice plans
for S3a + S3b. The doctrinal anchor (whitepaper V10 §5.7 + the
2026-05-27 integration spec) is the load-bearing reference; this spec
refines §2.3 from option A to option B + locks the §3-recipe dev-loop
asset path solution.*
