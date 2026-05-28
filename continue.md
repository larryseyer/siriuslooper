# Session Continuation — S1 design spec landed (Option D = OTTOEngine). Implementation deferred to next session.

## ▶ 0. Read these first (60 seconds)

This session attempted S1 execution per `plans/2026-05-27-otto-host-embeds-ottoprocessor.md` and hit a critical CMake gap mid-flight: the plan assumed `OTTOPlugin` (OTTO's JUCE-created shared-code static lib) was reachable from IDA's build, but IDA only `add_subdirectory`s `otto-core`, not `otto-plugin`. Discovered at the link step of IdaTests.

After a five-option architectural fork discussion with the operator (Options A through E), **Option D was locked**: OTTO grows a sixth build shape called **OTTOEngine** — a STATIC library version of the shared plugin code, without plugin-format SDKs or app wrapper, with self-driven transport. The design spec is committed to this repo; the implementation has NOT yet started.

1. **Read the design spec FIRST:** `docs/superpowers/specs/2026-05-27-otto-engine-static-target.md`. Thirteen sections. The decision log (§4) walks through all five options with explicit rejection reasoning — do NOT relitigate. The OTTO-side surgery (§5) and IDA-side integration (§6) are concrete enough to execute directly OR to invoke `superpowers:writing-plans` against for a formal slice plan.

2. **The original S1 plan is INVALIDATED:** `plans/2026-05-27-otto-host-embeds-ottoprocessor.md` assumed `OTTOPlugin` as the link target. The new plan's only structural difference is T1 — instead of "add `getPlayerManager()` accessor" (the accessor already exists at `external/OTTO/src/otto-plugin/PluginProcessor.h:377-378`, another plan-author miss), T1 is "create OTTOEngine target + parameterize SharedPluginSources.cmake + push OTTO." T2-T5 carry over with the link target name corrected from `OTTOPlugin` to `otto::engine`.

3. **Commits this session, in order, all on `master`:**
   - `2c92b58` (docs-only, this session) — OTTOEngine static-target design spec + new memory entry + MEMORY.md index update + continue.md handoff (this commit).
   - Pre-session HEAD was `d5d4fae` (the now-invalidated S1 plan).
   - HEAD = `2c92b58` (verify with `git log -1 --oneline`).

4. **OTTO upstream activity this session:** none from this session. **Concurrent OTTO Claude session is active, scoped to TAPECOLOR** (operator-confirmed mid-session 2026-05-27). The 2026-05-27 IDA→OTTO EventBus brief in `external/OTTO/CROSS_PROJECT_INBOX.md` remains `needs-ack`. The TAPECOLOR session is touching `external/lsfx_tapecolor/` + OTTO's TAPECOLOR adapter — NOT `src/otto-plugin/CMakeLists.txt`, `cmake/SharedPluginSources.cmake`, or any path the new OTTOEngine work would touch. File-level collision is unlikely; if the TAPECOLOR session pushed first, `git pull --rebase` handles it.

5. **Baseline preserved.** ctest 790/791 unchanged this session (zero code changes — pure docs commit). lsfx_tapecolor pin `a812670`; sfizz pin `f5c6e29f`; OTTO pin `4cdbad3e`. Working tree clean (only the pre-existing `m external/sfizz` marker).

6. **No execution this session.** All exploratory `Edit`s to `otto-bridge/CMakeLists.txt`, `otto-bridge/src/OttoHost.cpp`, and `tests/OttoHostRenderTests.cpp` were reverted to clean baseline mid-session. Nothing in the source tree changed except the new docs.

---

## ▶ 1. The locked-in design — OTTOEngine in 6 bullets

(Full design in `docs/superpowers/specs/2026-05-27-otto-engine-static-target.md`. Memory entry: `project_otto_engine_static_target`. Six-bullet summary for orientation:)

1. **Sixth build shape.** OTTO's existing five shapes (Standalone, VST3, AU, CLAP, AUv3) don't fit IDA's embed need. OTTOEngine is the sixth — a STATIC library, alias `otto::engine`, no plugin-format SDK, no app wrapper.

2. **Same shared sources as OTTOPlugin.** Both consume `cmake/SharedPluginSources.cmake` (paramaterized by a new `OTTO_SHARED_SOURCES_ROOT` variable, default `${CMAKE_SOURCE_DIR}` for OTTO's own builds). Single source of truth preserved — when OTTO adds a plugin .cpp, both targets pick it up.

3. **Self-driven transport.** OTTO's internal `TransportTracker` is authoritative. IDA does NOT feed a `juce::AudioPlayHead` to the embedded `OTTOProcessor`. This preserves `project_otto_is_the_transport_source` exactly. IDA's transport bar (S3 work) is a VIEW + COMMAND surface over OTTO's transport, not a transport-of-its-own.

4. **No OTTO-top-level config dependency.** IDA `add_subdirectory`s `src/otto-engine` directly, skipping OTTO's top-level CMakeLists (which has its own pre-existing issue with a missing `OTTOConfig.cmake` — see spec §11 footnote). OTTOEngine's CMakeLists is self-contained, computes its own paths via `CMAKE_CURRENT_SOURCE_DIR`.

5. **Honest framing.** A library called "OTTOEngine" is not a plugin — preserves `project_otto_is_part_of_ida_not_a_plugin`. The plugin-format wrappers' feature loss (no Standalone-only surfaces, flat parameter automation, etc.) is sidestepped entirely.

6. **OTTO-side surgery cost: ~30 lines of new CMake + 1 paramaterization line.** No source-code changes. No behavior changes to OTTO's existing five shapes (OTTOPlugin unchanged byte-for-byte).

---

## ▶ 2. The five rejected options (so you don't propose them)

Per spec §4. Brief form for context:

- **Option A — `add_subdirectory(otto-plugin)` from IDA.** Rejected: needs ~15 OTTO-top-level vars + `${CMAKE_SOURCE_DIR}`-relative include repointing + drags in plugin-format bundle generation IDA doesn't want.
- **Option B — Compile shared sources directly into IdaOttoBridge.** Rejected: breaks single-source-of-truth, must be re-synced whenever OTTO adds plugin .cpps.
- **Option C — Revisit the brainstorm.** Rejected: locked decisions remain valid; only the CMake plumbing needed fixing.
- **Option E — Port OTTO transport into IDA, host OTTO as VST3.** Rejected: contradicts three locked memories (`project_otto_is_part_of_ida_not_a_plugin`, `project_otto_does_not_host_plugins`, `project_otto_is_the_transport_source`); transport-code drift forever; plugin-wrapper feature loss; iOS AUv3 nesting fragility; plugin scanner broken; OOP IPC overhead.

Decision log in spec §4 has the full reasoning. Future Claude: do NOT relitigate.

---

## ▶ 3. Next-session action plan

1. **Read** `docs/superpowers/specs/2026-05-27-otto-engine-static-target.md` end-to-end. Especially §4 (decision log), §5 (OTTO-side surgery exact), §6 (IDA-side integration exact), §7.2 (TAPECOLOR-session collision protocol), §8 (acceptance contract), §11 (do-NOT-do list).

2. **Read** `external/OTTO/CROSS_PROJECT_INBOX.md`. Confirm the 2026-05-27 EventBus brief is still `needs-ack` (or if TAPECOLOR session ack'd it, prune per protocol).

3. **Invoke `superpowers:writing-plans` against the spec** to produce a formal slice plan that supersedes `plans/2026-05-27-otto-host-embeds-ottoprocessor.md`. Inputs to writing-plans:
   - Spec §5 = OTTO-side T1 detail
   - Spec §6 = IDA-side T2-T5 detail (carries over from old plan with link-target rename `OTTOPlugin` → `otto::engine`)
   - Spec §7 = cross-project sequencing
   - Spec §8 = verification contract

4. **Execute the new plan** task-by-task. Atomic IDA commit at the end (per `CLAUDE.md` commit discipline). Push to origin/master.

5. **After S1 lands:** the next plan target is **S2 — OttoPane tab via `OTTOProcessor::createEditor()`** (closes the M-OTTO-4 audibility gap). S2 plan is written via `superpowers:writing-plans` against §7 row S2 of `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md`.

---

## ▶ 4. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| HEAD | `2c92b58` (docs-only commit this session) |
| `git status --short` | clean (sfizz submodule shows as `m` — expected; unrelated) |
| New spec | `docs/superpowers/specs/2026-05-27-otto-engine-static-target.md` (13 sections, ~30 KB) |
| New memory | `~/.claude/projects/-Users-larryseyer-IDA/memory/project_otto_engine_static_target.md` + MEMORY.md index updated |
| Whitepaper path | `docs/IDA_Whitepaper_V10.md` (unchanged this session) |
| Old S1 plan status | `plans/2026-05-27-otto-host-embeds-ottoprocessor.md` is INVALIDATED — its T1 (the `getPlayerManager()` accessor) is moot (accessor already exists); its T2-T5 are mostly accurate but need link-target rename `OTTOPlugin` → `otto::engine` |
| OTTO submodule pin | `4cdbad3e` — unchanged this session |
| sfizz / lsfx_tapecolor pins | unchanged |
| ctest baseline | **790/791** (preserved; zero code changes this session) |
| IDA app builds + links | yes (pre-session clean build still valid) |
| TAPECOLOR concurrent OTTO session | active; touching unrelated paths; coordination protocol in spec §7.2 |

---

## ▶ 5. Resume protocol for next chat

1. **Read this file** (you're doing it).
2. **Read the spec:** `docs/superpowers/specs/2026-05-27-otto-engine-static-target.md`.
3. **Check `external/OTTO/CROSS_PROJECT_INBOX.md`** for new `[FROM OTTO → IDA]` entries (TAPECOLOR session may have posted one). Ack + prune per protocol.
4. **Invoke `superpowers:writing-plans`** to formalize the spec's §5+§6 into a new implementation plan.
5. **Execute** the new plan.
6. **Atomic IDA commit** at the end (OTTO submodule bump + IDA-side files all in one commit). Push origin/master.
7. **Update continue.md again** at session end per `feedback_update_continue_md_every_session`.

Reference docs:
- **THIS SESSION'S OUTPUT:** `docs/superpowers/specs/2026-05-27-otto-engine-static-target.md` (canonical S1 design — read first)
- **Architectural parent:** `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md` (the broader OTTO integration architecture; §1.2 commitment 2's engineering realization is now this session's spec)
- **Invalidated plan:** `docs/superpowers/plans/2026-05-27-otto-host-embeds-ottoprocessor.md` (read for context only — do NOT execute as-written; a new plan supersedes)
- **Whitepaper V10:** `docs/IDA_Whitepaper_V10.md` (canonical "why" — §5.7 is the doctrinal anchor for OTTO embedding)
- **OTTO Stereo Mix plan (S7):** `docs/superpowers/specs/2026-05-27-otto-stereo-mix-output.md` (still valid; queued behind S1-S6)
- **Cross-project inbox protocol:** `external/OTTO/CROSS_PROJECT_INBOX.md` + matching `CLAUDE.md` sections in both projects

Memory (key entries, in execution-relevance order):
- `project_otto_engine_static_target` *(written this session)* — Option D locked, full implementation guidance pointer
- `project_otto_integration_locked_decisions` — the broader architecture (still current)
- `project_otto_is_part_of_ida_not_a_plugin` — framing rule (still current)
- `project_otto_is_the_transport_source` — transport authority (still current; OTTOEngine honors it)
- `project_otto_does_not_host_plugins` — IDA hosts plugins; OTTO doesn't (still current)
- `project_otto_as_output_mixer_source` — the 32-output flow (still current)
- `project_otto_is_a_submodule_now` — single source of truth (still current)
- `project_cross_project_inbox_protocol` — AI-to-AI handoff mechanics
- `feedback_ida_not_ada` *(written this session)* — voice-transcription quirk; silently normalize
- `feedback_clean_builds_only_for_testing` — relevant for S1 implementation handoff
- `feedback_sirius_done_right_and_complete` — no half-baked features

---

*End of session. Spec landed; implementation deferred. The original S1 plan path was clean architecturally but missed a CMake reality the original plan author didn't verify. The new path (OTTOEngine) is documented thoroughly enough that future Claude sessions can pick it up cold without re-running this whole investigation. ctest 790/791 baseline preserved (docs-only commit).*
