# OTTO LookAndFeel foundation (Sirius UI parity — sub-project A)

## Context

IDA's engine and persistence layers (M1–M8) are deep and tested, but the GUI
lagged far behind: `MainComponent` surfaced almost none of the engine, and the
app looked like stock grey JUCE. The operator's directive: IDA and OTTO are
sister apps and IDA must **look and work like OTTO** — LookAndFeel, fonts,
mixer, meters, pills/phrases, tabs, transport, beat counter — everything.

That is a multi-session program, decomposed into sub-projects:

- **A — OTTO L&F foundation** (this spec): vendor OTTO's LookAndFeel + fonts and
  apply app-wide. The fastest visible change and the base everything else needs.
- B — Mixer (OTTO `MixerPanel`/`CompactFaderStrip`/`FaderMeter`; engine metering +
  mute/solo). C — built-in EQ/comp/FX DSP ported from OTTO. D — transport + beat
  counter. E — pills/phrases + remaining surfaces. F — tape capture wiring +
  plugin-loading (M7 S9) fix.

## Decision: copy (vendor), not alias

OTTO and IDA ship **separately**, so each must build and ship on its own. A
cross-repo alias/symlink to OTTO's source would (a) make IDA unbuildable when
OTTO is absent or when OTTO's L&F grows an OTTO-only dependency, and (b) mean
changing IDA's look requires editing OTTO — which the operator forbade.
Copying keeps IDA self-contained and leaves OTTO untouched. To keep drift
near-zero, the files are copied **verbatim** with identical class names and an
identical binary-data namespace, so re-syncing is a straight file copy and the
`.cpp` needs zero edits. The proper long-term form is a shared **submodule** (a
one-time extraction out of OTTO), tracked as future work.

## What landed

- **Vendored source** (`ui/lookandfeel/`, byte-faithful from OTTO
  `src/otto-plugin/ui/`): `OTTOLookAndFeel.{h,cpp}`, `OTTOColours.h`. The class is
  `otto::OTTOLookAndFeel : juce::LookAndFeel_V4`; colours are hardcoded constants
  in `otto::Colours` (no external theme/state coupling — the dependency audit
  confirmed zero coupling to OTTO app types; all component-property reads fall
  back safely).
- **Fonts** (`assets/Fonts/`): the 11 faces the L&F actually loads — Roboto,
  Roboto Condensed, Orbitron (variable), Bricolage Grotesque (5 weights),
  JetBrains Mono (3 weights) — plus `assets/GUI/Fader Knob.png` and the SIL OFL
  license. (OTTO's logos and grain texture are deliberately NOT copied — the L&F
  never references them.)
- **CMake** (`ui/CMakeLists.txt`): a `IdaBinaryData` target via
  `juce_add_binary_data` with `HEADER_NAME "OTTOBinaryData.h"` / `NAMESPACE
  OTTOBinaryData` (kept identical so the vendored `.cpp` compiles unchanged), and
  a dedicated `IdaLookAndFeel` static lib compiled **without** IDA's strict
  `-Werror` warning flags (vendored third-party code, held byte-faithful rather
  than reformatted). Exposed as `Ida::LookAndFeel`.
- **App wiring** (`app/Main.cpp`, `app/CMakeLists.txt`): the application owns an
  `otto::OTTOLookAndFeel` and installs it via
  `juce::LookAndFeel::setDefaultLookAndFeel` in `initialise()` **before** the
  main window is created (the window reads the default L&F for its background),
  clearing it in `shutdown()`. Existing IDA views use `getLookAndFeel()`, so
  they re-skin automatically.

## Scope

In: the L&F infrastructure, fonts, and global application. Out (later
sub-projects): mixer strips / `FaderMeter` (B), per-view pixel-parity tuning,
Sirius-branded replacement of any OTTO-specific imagery, and the shared-submodule
extraction.

## Verification

- Clean CMake/Ninja configure + `IdaLookAndFeel` and `IDA` build
  green against IDA's JUCE.
- Operator eyes-on: launching the app (via a fresh Desktop alias to the canonical
  `build/.../Release/IDA.app`) shows the whole UI in OTTO's dark theme
  with OTTO's fonts — confirmed. (A stale Desktop Finder-alias to a deleted build
  was the cause of earlier "no visible change"; replaced with a symlink to the
  canonical build.)
