# Session Continuation — OTTO integration scope doc + cross-project inbox housekeeping closed

## ▶ 0. Read these first (60 seconds)

1. **OTTO integration scope+sequencing roadmap doc landed** at
   `docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`
   (commit `2c7d688`). 6 milestones (M-OTTO-1 … M-OTTO-6), dependency
   graph, suggested execution order, what each milestone unblocks. Reads
   as a companion to the existing 2026-05-22 OTTO integration design.
2. **lsfx_tapecolor submodule bumped** `d8b06b1 → a7ba9c3` (Phase 8).
   This is intentionally **not** the latest available SHA — there's a
   real regression in the post-Phase-8 history that blocks bumping
   further (details in §2 below). Phase 6/7/8 needs-acks closed; Phase
   9, Chow J-A, Thiran, Alignment, Bypass-click, Phase A, Phase B+C
   remain **needs-ack from OTTO** until the regression is resolved.
3. **OTTO submodule bumped** `d43c540 → c01460a` — captures only the
   cross-project inbox edits (ack + prune Phase 6/7/8, file an IDA→OTTO
   regression entry). No OTTO code changes; OTTO's main has a lot of
   intermediate work between the prior pin and this one that IDA isn't
   consuming yet — the inbox bump is the minimum-viable bump per
   protocol (it propagates the inbox state to OTTO's remote).
4. **The just-landed mixer-pane surgical-append slice** (previous session,
   commit `d1fe0b1`) is **awaiting operator eyes-on**. Pass criterion:
   adjust phrase / MON strip fader, then add another phrase or flip MON
   on for another input — the first strip's value survives. Pre-fix it
   was reset on every refresh tick.
5. **Baseline.** `master` at `d742533` (verify with `git log -1 --oneline`).
   Local == origin (the push went through).
6. **Test surface.** `ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" -j`
   → **776 / 776** on clean rebuild against the bumped submodules.
   `./build/tests/IdaTests "[tapecolor-adapter]"` passes all 5 cases.

---

## ▶ 1. What landed THIS chat (continuation of the previous session)

| Commit | Subject |
|---|---|
| `9167bad` | (previous session) docs: continue.md — Output Mixer pane surgical-append parity |
| `2c7d688` | docs: OTTO integration scope+sequencing roadmap (6 milestones) |
| `d742533` | chore: bump submodules — lsfx_tapecolor → a7ba9c3, OTTO → c01460a2 (inbox housekeeping) |

OTTO-side (pushed to OTTO's `origin/main`):
- `c01460a2` (OTTO) — inbox: ack+prune Phase 6/7/8 + file regression entry

The IDA-side `d742533` commit's `OTTO-Origin: c01460a2` trailer points
back; `git log --grep='OTTO-Origin\|Ida-Origin' --all` in either repo
surfaces the full cross-project trail.

---

## ▶ 2. The lsfx_tapecolor regression — do not bump past a7ba9c3 yet

IDA's `tests/TapeColorAdapterTests.cpp` pins three invariants:

- Test 405 — `TapeColoringSink BeforeWrite with a default-OFF processor is still passthrough`
- Test 524 — `TapeColorAdapter::process supports in-place invocation`
- Test 738 — `TapeColorAdapter::process after prepare is a dry passthrough by default (TAPECOLOR default OFF)`

All three assert that `lsfx::TapeColorProcessor` with `cfg.enabled == false`
yields a **bit-identical** in→out passthrough. At a7ba9c3 (Phase 8) they
pass. At 14b4920e (Phase B+C, latest in the inbox) they fail —
post-Phase-A scaling leaks through the OFF path: input `0.065263` becomes
output `0.000255`, a ~250× attenuation, exactly the symptom of
`kDigitalToFluxScale = 0.14f` being applied with the matched
post-J-A inverse skipped on the OFF branch.

I filed a `[FROM IDA → OTTO]` entry in the cross-project inbox at
`external/OTTO/CROSS_PROJECT_INBOX.md` describing this. OTTO's next
session will see it at session start and address it (per protocol,
operator is not in the loop). Once OTTO posts a fix, IDA can bump.

I did **not** bisect to identify which exact post-Phase-8 commit
introduced the leak — Phase A's `kDigitalToFluxScale` constant is
consistent with the magnitude, but earlier phases (Chow J-A port,
Thiran allpass, Alignment standards) may also contribute. OTTO's
Claude should bisect on its side.

**Architectural side note (also in the IDA→OTTO entry):** lsfx_tapecolor's
`origin/main` has evolved from "shared DSP module" into a full plugin
UI system (TapeColorEditor, TransportCluster, PresetBar, PresetManager,
GearSurfaceView, CharacterTabView, CalEqTabView, DynamicsTabView,
MotionTabView, ~10 factory presets). The module's scope has expanded
beyond what the original IDA+OTTO sharing arrangement assumed. This
likely warrants a standalone discussion about whether IDA continues
consuming the full lsfx_tapecolor or shifts to a narrower DSP-only
target. Out of scope right now; flagged.

---

## ▶ 3. Architecture notes worth carrying forward

### Note A — Cross-project inbox handshake is now established

The protocol described in both `CLAUDE.md` files (full-edit-autonomy,
mandatory session-start inbox read, `OTTO-Origin:` / `Ida-Origin:`
trailers, ack-then-prune on entries-with-resolution) actually works
in practice — verified by closing 3 of OTTO's 10+ needs-ack entries
this session.

The pruning rule (codified at OTTO commit `d43c540f` 2026-05-24, IDA
side rule in `~/.claude/CLAUDE.md`-equivalent) means the inbox stays
tractable. Long-term audit lives in `git log --grep='Ida-Origin\|OTTO-Origin'`.

### Note B — When OTTO and IDA disagree, IDA pins to what works

The reflexive instinct is "bump to the latest the inbox asks for."
This session demonstrates the more general rule: **IDA pins to the
SHA that satisfies IDA's own invariants**, even if that's older than
OTTO's latest request. The inbox is the negotiation channel for
resolving such disagreements; OTTO addresses the regression, then
IDA bumps again. Don't bump and then immediately rip out the bump.

### Note C — OTTO integration milestones are now sequenced

The 2026-05-22 OTTO integration design described 4 decisions (32-stereo
source, inbox protocol, internal-FX adapter, asset policy) but left
sequencing implicit. The 2026-05-26 scope doc fills that gap with 6
concrete milestones in dependency order:

- M-OTTO-1: Link `otto-core` (mechanical, derisks downstream)
- M-OTTO-2: `OttoHost` skeleton + instantiation
- M-OTTO-3: Transport subscription + IDA listener API (unblocks
  todo entry B Transport sync, and the transport-sync sub-features of
  C MIDI and D Video)
- M-OTTO-4: 32 OTTO outputs as Output Mixer channels (uses the
  surgical-append seam landed last session)
- M-OTTO-5: OTTO state + preset serialization
- M-OTTO-6: OTTO operator UI (largest, own design)

M-OTTO-1 is the natural next slice — mechanical, derisks the
downstream work, surfaces any surprise build deps in `otto-core`.

---

## ▶ 4. What's next

### (A) Begin M-OTTO-1 — link `otto-core` into IDA's build (recommended)

Mechanical slice. Read `external/OTTO/src/otto-core/CMakeLists.txt` to
understand what target it exports + what dependencies it needs. Add
`add_subdirectory(external/OTTO/src/otto-core)` (or equivalent) to
IDA's CMake. Wire `otto-core` into a target IDA already links (likely
`engine/` or a new `otto-bridge/` per the scope doc's recommendation).
Verify clean rebuild, ctest stays at baseline. **No new IDA code uses
`otto-core` symbols yet** — this slice just proves the link works.

Risks: OTTO's `otto-core` may need sfizz, Ableton Link, or other
transitive deps. Read OTTO's CMake before scoping.

Size: small. 1 commit. Operator-verifiable by build success.

Unblocks: M-OTTO-2 through M-OTTO-6.

### (B) Wait for OTTO to address the lsfx_tapecolor OFF-passthrough regression

The IDA→OTTO inbox entry is the negotiation. Next session can check
whether OTTO has posted a fix (look for a new `[FROM OTTO → IDA]`
entry in the inbox at session start). If yes, bump submodules to the
newer pin. If no, this is on OTTO; do unrelated work.

### (C) Operator eyes-on the previous session's surgical-append slice

Pre/post-fix behavioral test on phrase + MON strips. Quick and
operator-only.

Default recommendation: **(A) M-OTTO-1**. Mechanical, well-scoped, derisks
all OTTO-import work, doesn't depend on OTTO's response on the
TAPECOLOR regression.

---

## ▶ 5. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| HEAD | `d742533` (`git log -1 --oneline`) |
| `git status --short` | clean (the pre-existing `IDA_Naming_Decision.md` rename is unchanged from prior sessions) |
| lsfx_tapecolor pin | `a7ba9c3` (Phase 8) — intentionally not bleeding edge |
| OTTO submodule pin | `c01460a2` (inbox ack, no code changes vs prior pin's intermediate history) |
| ctest baseline | 776/776 on clean rebuild; tolerate up to 4 transient flakes |
| Operator eyes-on (still pending) | Phrase + MON strip fader survives a refresh tick that previously wiped it (from `d1fe0b1` slice) |

---

## ▶ 6. Resume protocol for next chat

1. Read this file (you're doing it).
2. **At session start: read `external/OTTO/CROSS_PROJECT_INBOX.md`** per
   the cross-project protocol. Look for any new `[FROM OTTO → IDA]` entry
   addressing the OFF-passthrough regression. Ack any new entries
   addressed to IDA. The IDA→OTTO regression entry I filed should still
   be needs-ack (no resolution from OTTO yet).
3. Pick from §4. Default (A) M-OTTO-1.
4. If picking (A), start by reading `external/OTTO/CMakeLists.txt` and
   `external/OTTO/src/otto-core/CMakeLists.txt` to understand the link
   surface.

Reference docs:
- **OTTO integration sequencing:** `docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md` (this session)
- **OTTO integration design:** `docs/superpowers/specs/2026-05-22-otto-integration-design.md` (4 foundational decisions)
- **Cross-project inbox protocol:** `external/OTTO/CROSS_PROJECT_INBOX.md` + the matching sections in both `CLAUDE.md` files
- **Surgical-append pattern:** commit `d1fe0b1` (output side) + `d01bd00` (input side, canonical template)
- Whitepaper: `docs/IDA_Whitepaper_V9.md`

Memory:
- `project_otto_is_the_transport_source` — captured 2026-05-26
- `project_otto_as_output_mixer_source` — 32 stereo outputs into Output Mixer
- `project_internal_fx_first_class` — EQ/CMP/RVB/DLY from OTTO submodule
- `project_otto_is_a_submodule_now` — submodule consumption model
- `project_cross_project_inbox_protocol` — AI-to-AI handoff mechanics

---

*End of session. OTTO integration roadmap is in place; submodule pins
are at the latest IDA-validated state; cross-project inbox reflects the
real handoff state. Next session opens with M-OTTO-1 (mechanical otto-core
link) or with whatever OTTO has posted in response to the regression.*
