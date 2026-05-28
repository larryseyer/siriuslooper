# Session Continuation — S2 PARTIAL LANDING. Transport bar design + asset path queued for brainstorm next session.

## ▶ 0. TL;DR (60 seconds)

S2 landed **partially**. The IDA-side code shape (Tasks 1-8 of the plan) is in place: `OttoHost::getProcessor()` accessor + `ida::OttoPane` Component + "OTTO" tab in MainComponent + 5 new headless tests passing. ctest 796/797 (the 1 not-run is the expected pre-existing `MainComponentPluginEditorTests_NOT_BUILT-b12d07c`). Clean Release build passes; `IDA.app` codesigns. Pushed.

Operator-verified GUI launch (Task 9) discovered **two issues that re-scope S2**:

1. **Transport bar design needs a fresh design pass.** My Task 2 OR'd `setEmbeddedInHost` into OTTO's `isPluginMode_`, hiding OTTO's unified TransportBar (where Play/Pause/tempo/meter/spectrum live). Operator clarified: ONE transport bar, mounted ABOVE the IDA tab strip, visible at all times regardless of selected tab — **option B**, not the spec's §2.3 "build a new IDA-side TransportBar widget" option A. Option B is a design change to the integration spec (§1.1 diagram + §2.3 component definition) and the operator wants to brainstorm it properly in the next chat using web example interface references.
2. **OTTO sample assets aren't loading inside IDA.** All players fall back to synth-mode because `otto::library::SamplerPresetLoader::findSamplerFolder()` probes hardcoded paths off `juce::File::currentExecutableFile` (which is `IDA.app`, not `OTTO.app`). Diagnosed in §3 below. Clean fix recipe ready; not landed.

The IDA-side code that DID land is good shape and doesn't need to be undone by the brainstorm. The disagreement is purely about where OTTO's TransportBar widget lives and how the assets-root override is plumbed.

**Next chat:** Brainstorm transport bar design (option B mechanics) + asset path injection. Then a new plan supersedes the queued §6 of this doc.

---

## ▶ 1. SHAs at end of session

| Thing | SHA | Note |
|---|---|---|
| IDA HEAD (origin/master) | **(filled at commit time)** | S2 partial — see §4 |
| OTTO HEAD (origin/main, IDA's pin) | **f2b6f6db** | Was `b7654144` pre-session; +2 OTTO commits this session |
| OTTO commit 1 (pushed earlier) | **fb5ff039** | `feat: OTTOProcessor::setEmbeddedInHost() + PreferencesDialog gate` — the §2.2 setup work |
| OTTO commit 2 (pushed earlier) | **f2b6f6db** | `fix: revert isPluginMode_ OR — embedded host must keep TransportBar visible` — the revert based on a wrong understanding of the design; see §2 |
| lsfx_tapecolor (IDA's pin) | **7219f05** | Unchanged this session |

Both OTTO commits carry `Ida-Origin: <pending>` trailers — the inbox entries reference them but the `IDA commit:` line in those inbox entries is the `(filled at IDA-side atomic commit time)` placeholder. Optional follow-up: backfill those via a docs-only OTTO commit. The durable cross-project record is `git log --grep='Ida-Origin'` regardless.

---

## ▶ 2. Where the design diverged (operator clarification mid-session)

The S2 integration spec at `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md` §1.2 commitment 4 + §2.3 says:

> A persistent IDA-wide transport bar drives OTTO's transport. Visible from every tab… The bar subscribes to the existing `IOttoTransportListener` for state mirroring and invokes OTTO's transport actions on the message thread for control.

I read that as **option A**: build a NEW IDA-side `TransportBar` widget that subscribes to OTTO's transport events and sends control commands back. Two widgets exist; IDA's is the operator-visible one; OTTO's is hidden.

Operator clarified **option B**: ONE widget. Reuse OTTO's existing `otto::ui::TransportBar` (which lives at `external/OTTO/src/otto-plugin/ui/components/TransportBar.{h,cpp}`). Mount one instance of it above IDA's tab strip — visible at all times. Hide it inside the OTTO tab (the `isPluginMode_` OR was actually correct for this — but I had reverted it during the design uncertainty, see commit `f2b6f6db`).

Operator's exact words: *"OTTO's Top Bar is supposed to be IDA's top bar... we only need ONE transport bar... the transport should be visible AT ALL TIMES regardless of which tab is selected... It should sit ABOVE the tabs."*

This is a real design decision that needs a brainstorm session with web example references. The current spec needs to be updated to reflect option B before any code change.

---

## ▶ 3. Asset path diagnosis (recipe ready, not landed)

Operator-verified: every player falls back to synth-mode (no LSAD drumkits, no percs/shakers/hands).

Root cause: `otto::library::SamplerPresetLoader::findSamplerFolder()` (in `external/OTTO/src/otto-core/include/otto/library/SamplerPresetLoader.h:301-471`) probes a hardcoded ladder rooted at `juce::File::getSpecialLocation(juce::File::currentExecutableFile)`:

1. `<exe>/../../Resources/Sampler` (i.e. bundle Resources)
2. `<.app sibling>/assets/Sampler`
3. Dev-tree fallbacks
4. `~/Library/Application Support/OTTO/Sampler`

Inside IDA, `currentExecutableFile` is `IDA.app/Contents/MacOS/IDA`. None of those probes hit the operator's real assets at `/Users/larryseyer/AudioDevelopment/OTTO/assets/Sampler`. Same problem affects the IR loader and `otto::paths::PresetPaths::getRoot(StorageTier::Factory)`.

Assets are gitignored (copyright). Build-time copy into `IDA.app/Contents/Resources/Sampler` is the install-time fix but not the dev-loop fix.

**Recommended dev-loop + install fix (cross-project edit):**

Layer A — OTTO-side: new singleton `otto::paths::AssetsRoot` with `setOverride(juce::File)` / `get()` / `samplerFolder()` / `irFolder()` / `factoryPresetsFolder()`. Refactor the 3 existing path-resolution call sites to consult it first, falling through to their existing per-platform ladders when unset. OTTO standalone's behavior is unchanged (no `setOverride` call → existing probes run verbatim).

Layer B — IDA-side: call `otto::paths::AssetsRoot::setOverride(<assets path>)` from `OttoHost::Impl::Impl()` before `prepareToPlay`, sourced from a new `IDA_OTTO_ASSETS_DIR` CMake compile-def (default to `IDA_OTTO_WORKING_ROOT/assets`).

Layer C — installer: copy OTTO's assets into `IDA.app/Contents/Resources/` at install time; `setOverride` points there in production builds.

Full diagnostic + recipe in agent transcript `/private/tmp/claude-501/.../a5f62e3314b852242.output`. Roughly 80 lines of OTTO refactor + 5 lines of IDA call site + inbox protocol round-trip. Bounded scope.

---

## ▶ 4. What landed (the IDA atomic commit + the 2 OTTO commits)

### IDA-side (this session's atomic commit)

- **`otto-bridge/include/ida/OttoHost.h`** — added `namespace juce { class AudioProcessor; }` forward decl + `juce::AudioProcessor& getProcessor() noexcept` public accessor. Header stays JUCE-include-free.
- **`otto-bridge/src/OttoHost.cpp`** — implemented `getProcessor()`; `Impl::Impl()` calls `processor->setEmbeddedInHost(true)` as first body statement.
- **`app/OttoPane.{h,cpp}`** (new) — `ida::OttoPane final : public juce::Component`, owns `std::unique_ptr<juce::AudioProcessorEditor>`, ctor constructs editor via `host.getProcessor().createEditor()`, `resized()` forwards bounds, `paint()` fallback when editor null. Header forward-declares `juce::AudioProcessorEditor` and stays minimal.
- **`app/MainComponent.{h,cpp}`** — `#include "OttoPane.h"`; new `std::unique_ptr<ida::OttoPane> ottoPane_` member declared AFTER `ottoHost_` (line 373 vs 458); `tabs_.addTab("OTTO", ...)` inserted between Output Mixer and Tapes at line ~5670.
- **`app/CMakeLists.txt`** — `OttoPane.cpp` registered in IDA target sources.
- **`tests/OttoHostProcessorAccessTests.cpp`** (new) — 3 cases tagged `[otto-host-processor-access]`: vendor returns runtime-OTTOProcessor, `isEmbeddedInHost()` flipped at construction, pointer-stable across calls.
- **`tests/OttoPaneTests.cpp`** (new) — 2 cases tagged `[otto-pane-construction]`: constructs with non-null editor child, `resized()` forwards bounds.
- **`tests/CMakeLists.txt`** — new test sources registered + `${CMAKE_SOURCE_DIR}/app/OttoPane.cpp` pulled into IdaTests (mirrors `MainComponentPluginEditorTests` pattern since the app is `juce_add_gui_app`, not a static lib) + `${CMAKE_SOURCE_DIR}/app` added to IdaTests include dirs + `otto::engine` added to IdaTests link line (PluginProcessor.h + OTTOProcessor RTTI).
- **`external/OTTO`** submodule pointer bumped `b7654144` → `f2b6f6db`.
- **`docs/superpowers/plans/2026-05-27-otto-pane-s2.md`** (new) — the S2 plan as authored (10 tasks). The plan's Task 2 hide-via-isPluginMode_ step is now known-wrong per §2 above; Task 10 was atomized differently than the plan wrote it (operator-verified discovery → partial landing).

### OTTO-side (pushed during this session — already on `origin/main`)

- **`fb5ff039`** — added `OTTOProcessor::setEmbeddedInHost(bool)` / `isEmbeddedInHost() const` accessor pair + `std::atomic<bool> embeddedInHost_ { false }` storage; OR'd `proc.isEmbeddedInHost()` into `isPluginMode_`; passed `processor_.isEmbeddedInHost()` to `PreferencesDialog`; `PreferencesDialog` ctor now takes `bool embedded = false` + gates `OutputRoutingSection` add. Inbox entry filed (`Status: needs-ack`).
- **`f2b6f6db`** — reverted the `isPluginMode_` OR (kept everything else). Reverted based on the wrong understanding that OTTO's TransportBar should stay visible inside the OTTO tab. Per §2 above, the actual decision is option B (hide inside the tab AND mount above tabs) — but the relocation half is not landed. So `f2b6f6db` is currently the wrong direction for option B, but the right direction for the half-baked option-A interpretation. Next session decides whether to revert-the-revert (re-apply the OR) once the design is locked.

### What did NOT land

- IDA-side TransportBar (whatever shape — option A widget or option B relocation of OTTO's widget). Pending design brainstorm.
- Asset path override (`otto::paths::AssetsRoot` singleton + IDA `setOverride` call). Pending design brainstorm OR direct implementation if operator OKs the recipe in §3.
- The `Ida commit: <pending>` placeholders in OTTO's 2 inbox entries. Optional cleanup.

### Operator-visible state right now

If you launch `build/app/IDA_artefacts/Release/IDA.app` against the freshly-committed master:

- "OTTO" tab is visible between Output Mixer and Tapes. ✅
- Click the OTTO tab → OTTO's full editor UI renders. ✅
- OTTO's unified TransportBar (logo + play/pause + tempo + meter + spectrum + BeatProgressBar) IS visible inside the OTTO tab. ✅ (per the `f2b6f6db` revert)
- BUT: every player's kit picker shows only the synth mode — sample-based LSAD/percs/shakers/hands kits do not load. ❌ (§3 asset path bug)
- BUT: there's NO transport bar at IDA's top level — switching to any tab other than OTTO leaves no transport surface. ❌ (the option-B relocation is what's missing)

---

## ▶ 5. Baseline at end-of-session

| Check | Result |
|---|---|
| `git rev-parse HEAD` (IDA) | (filled at commit time) — pushed to origin/master |
| `git ls-tree HEAD external/OTTO` | f2b6f6db (post-bump) |
| `git ls-tree HEAD external/lsfx_tapecolor` | 7219f05 (unchanged) |
| `git status --short` | clean except pre-existing `m external/sfizz` |
| `ctest --test-dir build` | **796 passed, 1 not-run** — baseline preserved (+5 new tests from S2: 3 processor-access + 2 OttoPane-construction; the 1 not-run is the expected `MainComponentPluginEditorTests_NOT_BUILT-b12d07c`) |
| `cmake --build build --target IDA` | succeeds; `IDA.app` codesigned |
| OTTO origin/main HEAD | f2b6f6db |
| OTTO `[FROM OTTO → IDA]` entries | 0 |
| OTTO `[FROM IDA → OTTO]` entries | 4 outstanding (M-OTTO-3 EventBus brief + getPlayerManager accessor + OTTOEngine static target + setEmbeddedInHost flag — none of which need IDA-side action; OTTO's next session acks) + 1 more (`f2b6f6db` revert) for 5 total |

---

## ▶ 6. Resume protocol for the next chat

### Step 1: Read this file

### Step 2: Brainstorm — Transport Bar (option B mechanics) using web example interface references

Use `superpowers:brainstorming`. Key questions to resolve:

1. **Component reuse mechanics.** `otto::ui::TransportBar` is in `external/OTTO/src/otto-plugin/ui/components/TransportBar.{h,cpp}`. Default ctor, listener-based callbacks (`TransportBarListener` interface with `playPauseClicked`/`stopClicked`/`tempoChanged`/`tapTempo`). Can IDA construct its own instance and listener-bind to drive `OTTOProcessor` directly?
2. **Layout impact.** Mounting it ABOVE the tab strip changes `MainComponent::resized()`. How tall? Operator's pro-audio convention?
3. **What gets hidden inside the OTTO tab.** With IDA's top-level bar present, OTTO's `transportBar_` inside `OTTOEditor` must be invisible (or replaced) — the operator wants ONE bar, not two stacked. Revisit the `isPluginMode_` gate.
4. **State sync.** OTTO's `processBlock` already publishes transport state via `EventBus<TransportEvent>` which `OttoHost` marshals to listeners. IDA's TransportBar instance subscribes and reflects state; user inputs flow back via `OttoHost` method calls on the message thread.
5. **What happens before any OTTO transport activity.** Initial state, layout when OTTO isn't playing, BPM at 0 vs default.
6. **Web examples to reference.** Operator will bring them. Likely DAW transport bars (Ableton Live, Logic Pro, Pro Tools, Reaper).
7. **Asset path injection.** The §3 recipe is independent of the transport design — could land in parallel as its own slice, or fold into the same atomic commit as the transport relocation. Operator's call.

Output: an updated `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md` §2.3 (or supersede it) + a new plan against the updated spec.

### Step 3: After the brainstorm, write the plan via `superpowers:writing-plans`

The plan should:
- Restore the embedded-mode `isPluginMode_` OR (revert-the-revert) IF the design needs OTTO's tab-internal TransportBar hidden. (If the design has OTTO's bar reused above the tabs, then `f2b6f6db`'s revert was wrong-direction and needs to be re-reverted via a new OTTO commit.)
- Mount IDA's transport surface above the tab strip.
- Land the `otto::paths::AssetsRoot` override + IDA setOverride call.
- Re-verify GUI by operator with the spec'd checklist.

### Step 4: Inbox SHA backfill (optional cleanup)

The 2 OTTO commits this session (`fb5ff039`, `f2b6f6db`) have `Ida-Origin: <pending>` trailers and the inbox entries' `IDA commit:` lines still say `(filled at IDA-side atomic commit time)`. A docs-only OTTO commit can backfill both with the actual IDA atomic SHA. Low-priority — the durable record is `git log --grep='Ida-Origin'`.

---

## ▶ 7. Reference docs

- **In-tree S2 plan (PARTIAL LANDING):** `docs/superpowers/plans/2026-05-27-otto-pane-s2.md` — Tasks 1-7 IDA-side + 1-3 OTTO-side landed; Task 9 surfaced design gaps; Task 2's `isPluginMode_` OR is known-wrong direction for either A or B half-built.
- **Integration spec (needs update post-brainstorm):** `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md` — §2.3 TransportBar definition is option-A-shaped; needs option-B revision.
- **Engine-static spec (unchanged):** `docs/superpowers/specs/2026-05-27-otto-engine-static-target.md` — S1's durable architectural record.
- **Whitepaper V10 §5.7** — doctrinal anchor; "from the operator's perspective OTTO is the transport when OTTO is active" remains the load-bearing assertion regardless of A/B.
- **Cross-project inbox protocol:** `CLAUDE.md` (IDA) + `external/OTTO/CLAUDE.md` (OTTO) — both current.

---

*End of session. S2 partial. Transport bar design + asset path resolution queued for brainstorm next chat. ctest 796/797. IDA `(filled at commit time)` / OTTO `f2b6f6db` / lsfx_tapecolor `7219f05`.*
