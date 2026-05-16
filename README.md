# Sirius Looper

**A reference architecture for time-domain audio/video looping.**
Capture continuously. Compose retroactively. Never miss a take.

> **Status:** In development / alpha. macOS only. Not for end-user distribution.

---

## What it is

Sirius Looper is a performance looper for live and studio musicians. Every input
is captured to a continuously-running tape — whether or not the performer
intends to use it — so the moment is already recorded by the time the gesture
to keep it arrives. The performer composes phrases *retroactively*, marking in
and out points against material that has already happened, instead of fighting
to start a take on the downbeat.

Mixing, EQ, dynamics, and mastering live downstream in a DAW. The looper
handles arrangement — and arrangement is a creative act, not a processing one.

For the full design rationale, conceptual time model, and architectural
constraints, read the [**Whitepaper**](docs/Sirius%20Looper%20Whitepaper%20V2.md).
The Whitepaper is the canonical "why"; this README is the operator-facing
"what."

---

## Who it's for

Live performers operating in a reflexive, eyes-on-the-instrument state, and
studio musicians operating in a deliberate, analytical state. The instrument
is one; the presentation adapts. There is no "mode switch" — the system reads
context and the UI follows.

---

## Key concepts

| Term | Meaning |
| --- | --- |
| **Always-running tape** | Every input is being recorded, all the time, for the life of the session. |
| **Arm / Disarm** | Stands the capture gestures up (red) or stands them down. Sirius boots disarmed on purpose. |
| **Mark In** | Stamps the start of a captured region at the playhead. Re-tappable without losing armed state. |
| **Mark Out** | Stamps the end of the region and auto-promotes it into the song immediately. |
| **Phrase / Pill** | An auto-promoted captured region. Pills are the rounded rectangles on the timeline. |
| **Loop promotion** | Mark In inside an existing Phrase adds a Loop child; outside any Phrase mints a new Phrase. *(In flight — see below.)* |
| **Constituent hierarchy** | `tape → loop → phrase → section → song → set` — one unified tree, each level in its own time domain. |
| **Timeline view** | Horizontal ruler with an amber playhead chevron; one row per input tape; Pills laid out left-to-right. |

For the full glossary and step-by-step workflows, read the
[**User Guide**](docs/Sirius%20Looper%20User%20Guide.md).

---

## Build (macOS)

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

Requires CMake 3.22+, a C++20 toolchain, and Ninja. Vendored dependencies
(JUCE, soxr, Catch2) live under `external/` and are populated by
`bash/setup-deps.sh`.

---

## Documentation

| File | Purpose |
| --- | --- |
| [`docs/Sirius Looper Whitepaper V2.md`](docs/Sirius%20Looper%20Whitepaper%20V2.md) | Canonical "why": architecture, conceptual time, design philosophy. **Start here.** |
| [`docs/Sirius Looper User Guide.md`](docs/Sirius%20Looper%20User%20Guide.md) | Operator-facing "how": gestures, workflows, glossary. |
| [`docs/Sirius Looper Whitepaper V1.md`](docs/Sirius%20Looper%20Whitepaper%20V1.md) | Historical predecessor. V2 supersedes it; kept for reference. |
| [`continue.md`](continue.md) | Rolling session handoff: what's working, what's next. |
| [`todo.md`](todo.md) | Permanent design backlog and deferred work. |

---

## License

Sirius Looper is licensed under the **GNU Affero General Public License v3
(AGPLv3)** with an **Apple App Store distribution exception** and a **sample
library exclusion**. The licensing model is identical to the sister application
OTTO.

| File | Covers |
| --- | --- |
| [`LICENSE`](LICENSE) | Primary license: AGPLv3 + App Store exception + sample exclusion. |
| [`licenses/AGPL-3.0.txt`](licenses/AGPL-3.0.txt) | Full text of the GNU Affero General Public License v3. |
| [`docs/SAMPLE-LICENSE.md`](docs/SAMPLE-LICENSE.md) | Terms governing the Larry Seyer Acoustic Drum Library (FLAC + SFZ). |
| [`docs/LICENSE-THIRD-PARTY.md`](docs/LICENSE-THIRD-PARTY.md) | Attributions and terms for bundled third-party components. |

All four documents apply.

The AGPLv3 covers source code only. Bundled audio content (the drum library)
is proprietary and separately licensed; compiled binaries may be distributed
through Apple's App Store provided corresponding source remains freely
available under AGPLv3.

---

## Sister project

Sirius Looper is a companion to [**OTTO**](https://github.com/larryseyer/OTTO),
a drum machine and groove engine. Both apps share the same audio architecture,
the same look-and-feel system, and identical licensing terms.

---

## Status notes

- **Working:** TimelineView with playhead overlay; CaptureBanner transient
  confirmation; four-tab UI (Performance / Preparation / Plugins / Video);
  always-running tape capture; arm/disarm; Mark In / Mark Out; undo/redo;
  persistence + descriptor format; ensemble merge logic (headless).
- **In flight:** Loop Constituent promotion — captured regions currently live
  in RAM only and evaporate on app exit. Persistence to disk is the next
  brainstorm.
- **Deferred:** OTTO Look-and-Feel integration (shared submodule, module home
  TBD); plugin scanner crash investigation; video pipeline (M6); ensemble
  networking (M8); CI matrix push; Windows and Linux build verification.

Tests: see `./build/tests/SiriusTests`. CMake-based build, macOS-only as of
this writing. Project memory and ongoing work are tracked in `continue.md` and
`todo.md`.
