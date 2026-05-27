# Session Continuation — M-OTTO-1 landed (otto-core linked into IDA's build)

## ▶ 0. Read these first (60 seconds)

1. **M-OTTO-1 complete.** Two commits this session pushed to `origin/master`:
   - `22a1c5a` — M-OTTO-1a: vendor sfizz as submodule (`f5c6e29f`, matches
     OTTO) + 7 sub-submodules recursive (abseil-cpp, filesystem, invoke.hpp,
     simde, dr_libs, libaiff, wavpack) + 64-channel patch copied from OTTO.
   - `674ac96` — M-OTTO-1b: FetchContent Ableton::Link + add_subdirectory
     sfizz (configure-time 64-channel patch) + add_subdirectory otto-core
     (OTTO_ENABLE_GENERATION=OFF) + `otto::core` linked into IDA app target.
2. **Scope estimate was wrong.** The 2026-05-26 scope doc said "Size: small.
   1 commit." Reality: 2 commits, +100MB+ submodule tree, ~13 sec CMake
   configure time added. The scope's flagged risk ("otto-core may have
   build dependencies IDA doesn't already have — sfizz, Ableton Link")
   materialized — both. **Update the scope doc when revising.**
3. **No IDA code uses otto-core symbols yet.** Static-archive linker will
   dead-strip them on the final IDA binary. The link is wired
   (`otto-core/libotto-core.a` is in IDA's link line — verified via
   `build/build.ninja` grep). First real consumer lands in M-OTTO-2
   (`OttoHost` skeleton).
4. **Baseline.** `master` at `674ac96` (verify with `git log -1 --oneline`).
   Local == origin (push went through).
5. **Test surface.** `ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" -j`
   → **776 / 776** on clean rebuild against `master`. Two transient
   flakes hit this session (test #295 + #470, both `ida_plugin_host`
   supervisor) but pass on retry — within the documented 4-flake tolerance.
6. **OTTO inbox.** No new `[FROM OTTO → IDA]` entries this session. The
   2026-05-26 IDA → OTTO regression entry (TAPECOLOR OFF-passthrough
   leak after Phase A) is still `needs-ack` from OTTO's side. The Phase
   9 / Chow J-A / Thiran / Alignment / Bypass-click / Phase A / Phase B+C
   needs-acks from OTTO remain pinned in-place per the prior session's
   decision (don't bump lsfx_tapecolor past `a7ba9c3` until OTTO fixes
   the regression).

---

## ▶ 1. What landed THIS chat

| Commit | Subject |
|---|---|
| `22a1c5a` | chore: vendor sfizz as submodule (M-OTTO-1a) — pinned f5c6e29f, 7 sub-submodules recursive, 64-channel patch copied from OTTO |
| `674ac96` | feat: link otto-core into IDA app target (M-OTTO-1b) — FetchContent Ableton::Link + add_subdirectory sfizz/otto-core, OTTO_ENABLE_GENERATION=OFF, ctest 776/776 |

OTTO-side: none. The cross-project inbox was not touched this session.

---

## ▶ 2. CMake / build notes worth carrying forward

### Note A — sfizz's working tree is permanently "dirty"

`external/sfizz/src/Config.h.in` is modified in-place by the configure-time
patch (the 64-channel fix from `patches/sfizz-max-channels.patch`). CMake
generates `src/sfizz/Config.h` from the patched template each configure.
Both files show as modified under `git -C external/sfizz status`. This
matches OTTO's pattern (their sfizz submodule is dirty for the same
reason). Do NOT try to "fix" this by reverting the patch — it's the
required state for the 32-stereo-pair (64-mono) output topology IDA
inherits from OTTO. The CMake step in `cmake/Dependencies.cmake` is
idempotent: it greps Config.h.in for `maxChannels { 64 }` first and
skips re-patching if already done.

### Note B — sfizz's CMake emits warnings during configure

Non-fatal: missing-but-optional Qt5 (UI tools, OFF), missing-but-optional
samplerate (we use sfizz's internal resampler, also OFF), a stray `elseif`
with no args in sfizz's SfizzDeps.cmake. These are sfizz upstream warts,
not IDA bugs. Don't try to silence them by editing sfizz.

### Note C — Ableton::Link is FetchContent, not vendored

Pinned to `addb7da` (Feb 2026, same SHA OTTO uses). First configure-time
checkout pulls from `github.com/Ableton/link.git` and caches under
`build/_deps/link-src`. A clean rebuild (`rm -rf build`) re-downloads.
If long-term network reliability becomes a problem, switching to a
vendored submodule is a 10-line change to `cmake/Dependencies.cmake`.

### Note D — OTTO_ENABLE_GENERATION is forced OFF in IDA

OTTO's top-level CMake defaults this ON (TF Lite-driven in-app pattern
generation — OTTO product surface). IDA forces it OFF because (a) IDA
isn't shipping that feature, (b) TF Lite drags a ~200MB tensorflow
submodule + a non-trivial cmake/TFLite.cmake bootstrap, (c) otto-core's
generation/*.cpp sources are wholly gated on the flag. If a future IDA
feature wants OTTO's MIDI generation pipeline, flip the flag and pull
in the tensorflow submodule. Today the IDA build is leaner without it.

### Note E — Link order: otto-core appears in IDA app's LINK_LIBRARIES

Verified by `grep "libotto-core" build/build.ninja`. The line includes
`otto-core/libotto-core.a` plus its transitive sfizz + abseil + Link
dependencies. The static-archive linker dead-strips otto-core symbols
in this slice because nothing in IDA references them yet — that's
expected per M-OTTO-1's "no code uses otto-core" criterion. M-OTTO-2's
`OttoHost` will be the first real consumer.

---

## ▶ 3. What's next

### (A) Begin M-OTTO-2 — `OttoHost` skeleton + instantiation (recommended)

Per the 2026-05-26 scope doc (`docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`):

- New module: small `otto-bridge/` library between `engine/` and `app/`.
  JUCE-free public header (`OttoHost.h` with opaque type + an
  `IOttoTransportListener` interface); JUCE-coupled `.cpp` that holds
  the OTTO instance.
- `MainComponent` constructs an `OttoHost` in its ctor; dtor tears down.
- `prepareToPlay(sampleRate, blockSize)` propagates into OTTO.
- No audio output, no transport, no UI surfacing yet — that's M-OTTO-3
  + M-OTTO-4.

Size: medium. Per the scope doc, 2-3 commits (CMake skeleton, ctor/dtor,
MainComponent wiring). Operator-verifiable by `OttoHost` constructing
+ destructing across the IDA app lifecycle without crashes.

**Layer placement decision** (scope doc M-OTTO-2 risk note): new
`otto-bridge/` module is cleaner than putting it in `engine/`, because
engine's RT-safety contract would otherwise need an "OTTO is exempt"
carve-out everywhere. The new-module path localises the JUCE coupling
to one place.

### (B) Wait for OTTO to address the lsfx_tapecolor OFF-passthrough regression

Same status as last session. Next-session check: look for a new
`[FROM OTTO → IDA]` entry in `CROSS_PROJECT_INBOX.md` addressing the
regression. If yes, bump submodules to the newer pin. If no, this is
on OTTO; do unrelated work (M-OTTO-2 is the natural choice).

### (C) Operator eyes-on the surgical-append slice (from 2 sessions ago)

Still pending. Pre/post-fix behavioral test: adjust phrase / MON strip
fader, then add another phrase or flip MON on for another input — the
first strip's fader value survives. Pre-fix it was reset on every
refresh tick. Commit `d1fe0b1`. Quick, operator-only.

Default recommendation: **(A) M-OTTO-2**. M-OTTO-1 unblocked it; the
otto-bridge module is the next concrete piece and doesn't depend on
OTTO's regression response.

---

## ▶ 4. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| HEAD | `674ac96` (`git log -1 --oneline`) |
| `git status --short` | clean (sfizz submodule shows as `m` — expected, see Note A) |
| lsfx_tapecolor pin | `a7ba9c3` (Phase 8) — intentionally not bleeding edge |
| OTTO submodule pin | `c01460a2` — unchanged from last session |
| sfizz submodule pin | `f5c6e29f` (NEW — matches OTTO's pin) |
| ctest baseline | 776/776 on clean rebuild; 2 transient flakes pass on retry |
| IDA app builds + links otto-core | yes — `build/build.ninja` shows `otto-core/libotto-core.a` in IDA's LINK_LIBRARIES |
| Operator eyes-on (still pending) | Phrase + MON strip fader survives a refresh tick that previously wiped it (from `d1fe0b1`) |

---

## ▶ 5. Resume protocol for next chat

1. Read this file (you're doing it).
2. **At session start: read `external/OTTO/CROSS_PROJECT_INBOX.md`** per
   the cross-project protocol. Look for any new `[FROM OTTO → IDA]`
   entry — especially one addressing the OFF-passthrough regression.
   Ack any new entries addressed to IDA. The 2026-05-26 IDA → OTTO
   regression entry should still be `needs-ack` until OTTO fixes it.
3. Pick from §3. Default (A) M-OTTO-2.
4. If picking (A), the scope doc + 2026-05-22 design doc are the
   reference. Start by sketching `otto-bridge/CMakeLists.txt` (small
   static library) + `otto-bridge/include/ida/otto/OttoHost.h` (the
   JUCE-free public header).

Reference docs:
- **OTTO integration sequencing:** `docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`
- **OTTO integration design (4 foundational decisions):** `docs/superpowers/specs/2026-05-22-otto-integration-design.md`
- **Cross-project inbox protocol:** `external/OTTO/CROSS_PROJECT_INBOX.md` + the matching sections in both `CLAUDE.md` files
- Whitepaper: `docs/IDA_Whitepaper_V9.md`

Memory:
- `project_otto_is_a_submodule_now` — submodule consumption model
- `project_otto_is_the_transport_source` — IDA has no engine-side transport state; OTTO supplies play/stop
- `project_otto_as_output_mixer_source` — 32 stereo outputs into Output Mixer (M-OTTO-4 target)
- `project_cross_project_inbox_protocol` — AI-to-AI handoff mechanics

---

*End of session. M-OTTO-1 landed cleanly across 2 commits; sfizz +
Ableton::Link + otto-core all link cleanly into IDA's app target;
ctest baseline holds. M-OTTO-2 is the next mechanical slice — small
otto-bridge module containing one `OttoHost` instance with no
audio, no transport, no UI yet.*
