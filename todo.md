# Sirius Looper — Deferred Items

### 2026-05-17 — V7 alignment milestone tracking — 24 milestones, M1 ready to start

- **Status:** Plan spec'd in full at
  `docs/superpowers/plans/2026-05-17-v7-alignment.md`. No code work
  this session — design + roadmap only.
- **Files:** plan + design-pointer spec landed in repo; `continue.md`
  RESUME HERE updated; `continue.md §6` marked superseded.
- **What's needed to finish each milestone:** see the plan file's
  per-milestone block (Goal, Acceptance, Dependencies, Files,
  Tests, Sessions 1-3, Verification, Risks, Execution mode).
- **Execution doctrine:** orchestrator+subagents default; ralph inner
  loop after PRD for M13, M19, M22, M24. Ralph is operator-launched
  per memory rule.
- **Open per-milestone sub-design parks** (deferred, will defrost as
  their parent milestone starts; each gets a fresh `todo.md` entry at
  defrost time):
  - M5 — EQ + dynamics DSP implementations (M5 ships gain + pan only)
  - M10 — transition Curve representation (M10 ships Cut + LinearFade only)
  - M14 — direct-routing rule table (sub-spec session)
  - V7 §18.3 UX-research items: control surface ergonomics; phrase-
    relationship UX; structured improvisation interfaces; AI assistance
    role; capability-tier auto-detection heuristics; real-world flush-
    interval validation
- **Milestone status tracker** (update as each completes):

| ID | Title | Status |
|----|-------|--------|
| M1 | Audio I/O foundation + RT-safety contract | not started |
| M2 | Membrane→Mixer rename + SignalType + Channel | not started |
| M3 | Channel-driven tape allocation + processing chains | not started |
| M4 | Direct Layer (manual routing) | not started |
| M5 | Output Mixer expansion: strips + buses + sends | not started |
| M6 | NotificationBus engine↔UI channel | not started |
| M7 | Out-of-process plug-in hosting framework | not started |
| M8 | Plug-in determinism + failure semantics + CLAP | not started |
| M9 | MIDI 2.0 / UMP end-to-end | not started |
| M10 | Mix snapshots | not started |
| M11 | Sirius Archive Format (clean break from JSON) | not started |
| M12 | Video tier-aware rendering | not started |
| M13 | File I/O + export (ralph inner loop) | not started |
| M14 | Automatic direct-routing inference | not started |
| M15 | LMC discipline tiers (GPS/PTP/NTP/Link) | not started |
| M16 | Ensemble consistency (vector clocks + partition) | not started |
| M17 | Ensemble security (libsodium + Noise + X25519) | not started |
| M18 | Inclusive-design surfaces | not started |
| M19 | Validation test harness (ralph inner loop) | not started |
| M20 | VST3 host | not started |
| M21 | AU host | not started |
| M22 | UI vocabulary cleanup (ralph inner loop) | not started |
| M23 | iOS AUv3 port | not started |
| M24 | Docs final alignment pass (ralph inner loop) | not started |

### 2026-05-16 — CI signing secrets — three of six set, three pending operator hands

- **Status:** half-done as of end of session 2026-05-16. Three repo
  secrets (`APPLE_TEAM_ID`, `APPLE_ID`, `KEYCHAIN_PASSWORD`) are live
  in the GitHub repo settings. Three remain (`DEVELOPER_ID_CERT_P12_BASE64`,
  `DEVELOPER_ID_CERT_PASSWORD`, `APPLE_APP_PASSWORD`) — these require
  the operator to drive Keychain Access (export .p12) and visit
  appleid.apple.com (generate app-specific password), neither of which
  Claude can automate.
- **Why deferred:** operator stepped out for ~8 hours mid-handoff;
  picks up when back. Full walkthrough in `continue.md` § "RESUME HERE".
- **What's needed to finish:**
  1. Export Developer ID Application cert as `~/Desktop/sirius-devid.p12`
     from Keychain Access (full steps in continue.md).
  2. Generate an app-specific password at appleid.apple.com.
  3. Either hand the values to Claude (Path A) or run the three
     `gh secret set` lines directly (Path B). Both spelled out in
     continue.md.
  4. `gh workflow run ci-macos-signed.yml` and watch it go green.
  5. Delete the local `.p12` after CI is green.

### 2026-05-16 — Auto-testing milestone — SUPERSEDED-AND-IMPLEMENTED 2026-05-16

- **Status:** Shipped end-to-end this session. All three phases of the
  approved plan landed on master.
- **What landed:**
  - **Phase A** — `bash/smoke-persistence.sh` patched and verified
    against a freshly-built signed bundle. Round-trip exits 0 with
    "refs in file: 2" (proves v2 shared-encoding ran). Commit `4e8a1df`.
    Five fixes total: direct binary launch (works around Launch
    Services -10825 on dev-tree paths), explicit two-level AX
    recursion (works around `entire contents` stripping role info),
    tab switch to Preparation before clicking Save, dialog-window-
    targeted action-button clicks with Return fallback when Open is
    disabled, and `key code 5` instead of `keystroke "g"` for
    Cmd+Shift+G (the modified-keystroke form silently no-ops on
    Sequoia inside NSSavePanel when the filename field has focus).
  - **Phase B** — `bash/autotest.sh` 4-phase local driver:
    headless ctest → signed Xcode build → codesign+spctl verify →
    GUI smoke. Snapshots `codesign -dvv` output once and greps the
    capture (repeated `codesign -dv` invocations were flaky in pipe
    chains). Commit `f68aa3c`.
  - **Phase C** — `.github/workflows/ci-macos-signed.yml` new
    workflow. macos-latest only; imports Developer ID cert from
    repo secrets into a temporary keychain, configures and builds
    the signed Xcode bundle, runs `codesign --verify` + greps the
    cert chain, runs `bash/smoke-persistence.sh` as a best-effort
    step (`continue-on-error: true` — Accessibility/TCC on GitHub-
    hosted runners is famously fragile). Triggered on push to master
    and via workflow_dispatch.
- **Operator one-time step before first CI run:** add six repo secrets
  per the workflow file's header comment (DEVELOPER_ID_CERT_P12_BASE64,
  DEVELOPER_ID_CERT_PASSWORD, APPLE_ID, APPLE_APP_PASSWORD,
  APPLE_TEAM_ID, KEYCHAIN_PASSWORD).
- **Followups filed during this milestone:** see the three new entries
  immediately below (`get-task-allow=true`, `open` -10825 on dev tree,
  `Sirius Looper` bundle-id collision). None are blocking; all surface
  the same underlying surface the milestone touched.

### 2026-05-16 — `com.apple.security.get-task-allow=true` in signed bundle (notarization blocker)

- **Files:** `app/CMakeLists.txt` (the `set_target_properties` Xcode-
  attribute block, lines ~66-104), possibly
  `app/SiriusLooper.macos.entitlements`.
- **What was deferred:** `codesign -dv --entitlements - "Sirius Looper.app"`
  on the freshly-built signed bundle reports two entitlements:
  ```
  [Key] com.apple.security.device.audio-input
  [Value] [Bool] true
  [Key] com.apple.security.get-task-allow
  [Value] [Bool] true
  ```
  `get-task-allow=true` is the debugger-can-attach flag — fine for
  local dev iteration, **rejected by Apple's notarization service**.
  Any release build that needs to be notarized must either set this
  to `false` or omit the key entirely. Likely a Xcode default for
  Release builds with `Automatic` signing style + non-App-Store
  destination; the CMake `XCODE_ATTRIBUTE_*` block needs an override
  (something like `XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS = NO`
  plus an explicit entitlement saying `get-task-allow=false`, or a
  `XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS` adjustment).
- **Why deferred:** the notarization-in-CI work was already deferred
  in the approved auto-testing plan ("Notarization in CI — defer" —
  it's network-bound and slow; meant for a future release-tag
  workflow). This is the same scope: handle once notarization actually
  happens. Local dev signing is unaffected.
- **What's needed to finish:**
  1. Either bake `get-task-allow=false` into
     `app/SiriusLooper.macos.entitlements` and verify Xcode doesn't
     re-inject the `true` version, OR add a `codesign --entitlements`
     post-build step that strips/replaces it, OR adjust the CMake
     XCODE_ATTRIBUTE flags.
  2. Verify with `codesign -dv --entitlements -` after a clean rebuild.
  3. Then a notarytool submit should succeed without the
     "get-task-allow is set" stapler error.
- **Surfaced by:** auto-testing Phase A inspection of the
  freshly-built signed bundle (2026-05-16).

### 2026-05-16 — `open ...app` returns -10825 on signed dev-tree bundle (Launch Services confusion)

- **Files:** none directly — this is a macOS Launch Services state
  issue, not a project code issue. Workaround already in place in
  `bash/smoke-persistence.sh` (direct binary launch instead of `open`).
- **What was deferred:** root-cause investigation. Symptoms:
  - `open "build-xcode/app/SiriusLooper_artefacts/Release/Sirius Looper.app"`
    → `_LSOpenURLsWithCompletionHandler() failed with error -10825`,
    no process launches. Every variant tried (`-n`, `-a`, `-F`,
    `lsregister -f` first) fails the same way.
  - `open "build/app/.../Sirius Looper.app"` (the ad-hoc-signed
    sibling) works fine.
  - `"build-xcode/.../Contents/MacOS/Sirius Looper"` launches fine
    directly (used by the smoke script now).
  - No quarantine xattrs on either bundle. `spctl -a -t exec -vv`
    accepts the signed bundle as `Developer ID`.
- **Working hypothesis:** Launch Services has cached the ad-hoc
  bundle's invalid `Identifier=Sirius Looper` (a string with a space,
  which is not a legal bundle identifier) and is rejecting the
  signed bundle which has the correct `com.larryseyer.siriuslooper`
  identifier with the same display name. The two same-named bundles
  in sibling dev-tree paths confuse LS in a way `lsregister -u/-f`
  doesn't fully clear.
- **What this means for operators:** end-user launches through
  Finder/Dock against a notarized bundle in /Applications are not
  affected — that's a fresh path with a fresh LS entry. But if an
  operator wants to double-click `Sirius Looper.app` from a dev tree
  in Finder, they may hit this -10825. Workaround: launch via
  Terminal directly with `"path/Contents/MacOS/Sirius Looper"`.
- **What's needed to finish:**
  1. Confirm hypothesis by `lsregister -dump | grep -i sirius` and
     looking for duplicate entries with different identifiers.
  2. If confirmed: either fix the `build/` ad-hoc bundle's identifier
     (see next entry), or add a CMake post-build step that resets
     LS for the bundle path.
  3. Optional: a `bash/launch-app.sh` helper that picks the right
     bundle and launches via the right mechanism, hiding the LS
     wart from operators.
- **Surfaced by:** auto-testing Phase A `open` failure during smoke
  script setup (2026-05-16).

### 2026-05-16 — `build/` ad-hoc bundle has invalid `Identifier=Sirius Looper` (should be reverse-DNS)

- **Files:** the Unix-Makefiles half of `app/CMakeLists.txt` — the
  `juce_add_gui_app` invocation or the JUCE_* CMake properties that
  set `CFBundleIdentifier`.
- **What was deferred:** `codesign -dv "build/app/.../Sirius Looper.app"`
  reports `Identifier=Sirius Looper` (literal string with a space).
  The Xcode-gen build correctly produces `Identifier=com.larryseyer.siriuslooper`.
  Both targets are the same `SiriusLooper` JUCE app target; the
  difference comes from CMake generator-specific defaults.
- **Why this matters:** invalid bundle identifiers (containing
  whitespace, lacking reverse-DNS structure) confuse Launch Services
  registration, file-type associations, and notarization. The
  previous entry's `open` -10825 issue is almost certainly downstream
  of this.
- **What's needed to finish:**
  1. Locate where the bundle identifier is set in `app/CMakeLists.txt`
     and either:
     - Set an explicit `BUNDLE_ID "com.larryseyer.siriuslooper"` in the
      `juce_add_gui_app` call (JUCE's documented mechanism), OR
     - Set `XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER` AND the
       equivalent Makefile-path property (CMake's
       `MACOSX_BUNDLE_GUI_IDENTIFIER` target property).
  2. Verify both `build/` and `build-xcode/` bundles report the same
     identifier after a clean rebuild.
- **Operational consequence today:** the ad-hoc dev bundle is the
  one operators iterate on. Its broken identifier was previously
  harmless (no signing, no LS-level integration). Now that the
  signed sibling exists, the collision is biting (see -10825 entry).
- **Surfaced by:** comparing `codesign -dv` output between the two
  build trees during auto-testing Phase A (2026-05-16).

### 2026-05-16 — CI green on Linux + Windows (clean up latent strictness errors)

- **Files:** `persistence/src/SessionFormat.cpp` (lines 248, 279, 305,
  308, 337, 425, 452, 556, 563 — nine sites), plus any further error
  layers that surface once these clear.
- **Status:** macOS CI green; Linux + Windows still red but the
  remaining errors are pre-existing strictness issues that Apple
  Clang masks. **Not blocking anything Claude or the operator does
  on macOS** — the dev loop, headless tests, and `.app` smoke test
  all run locally on Apple Clang where these compile fine. CI green
  is the right target before bringing the project to a non-Apple
  collaborator or expanding the runner matrix.
- **What's deferred:** GCC and MSVC report `conversion from 'int64_t'
  ... to 'const juce::var' is ambiguous` at nine call sites that
  pass a bare `std::int64_t` (the `ConstituentId::value()` return
  type) into `juce::DynamicObject::setProperty(name, value)`. JUCE's
  `var` constructor takes `int`, `int64`, `bool`, and others; on
  ambiguous integer widths, stricter compilers refuse to pick.
  Apple Clang silently picks `juce::int64`.
- **What was already done this session:**
  Promotion.cpp designated-initializer order fixed in commit
  `8dcf39d` (was the first layer of errors; clearing it exposed
  this second layer). `actions/checkout@v4 → @v5` bump in `9e94873`
  removed an unrelated deprecation warning.
- **Fix shape (mechanical):**
  Cast the int64 explicitly at the call site so overload resolution
  is unambiguous on every compiler. Either:
  ```cpp
  obj->setProperty ("ref", static_cast<juce::int64> (id));
  ```
  or use JUCE's own type for the storage:
  ```cpp
  juce::int64 id = c.id().value();
  obj->setProperty ("ref", id);
  ```
  Pick one and apply uniformly across the nine sites. A grep for
  `setProperty.*\.value\(\)` across `persistence/src/` should
  catch every offender; widen to other modules if any of them
  also call `setProperty` with raw int64.
- **What's needed to finish:**
  1. Apply the cast at the nine sites in `SessionFormat.cpp`.
  2. Local sanity build on macOS (must stay green).
  3. Push and watch one CI run end-to-end. If Linux + Windows
     surface a *third* layer of errors, repeat the same pattern:
     fix, push, watch. This is the cost of a long Apple-Clang-only
     dev period coming home to roost.
  4. Optional belt-and-suspenders: add an `-Werror` build mode
     gated behind `-DSIRIUS_STRICT=ON` and run it on macOS CI too,
     so the next divergence is caught locally instead of by a CI
     email loop.
- **Don't pursue inside another milestone session:** worth handling
  as its own focused 30-60 minute pass — the fix is mechanical but
  there may be successive layers, and interleaving with feature
  work makes the diffs hard to bisect later.

### 2026-05-16 — Developer ID signing milestone — SUPERSEDED-AND-IMPLEMENTED 2026-05-16

- **Status:** Implemented end-to-end 2026-05-16 (commit `128913a`).
- **What landed:**
  - `app/SiriusLooper.macos.entitlements` (audio-input only — sandbox-only
    keys like `files.user-selected.read-write` are unnecessary outside the
    sandbox; Info.plist `NS*UsageDescription` keys handle Load-dialog TCC).
  - `app/CMakeLists.txt` — `set_target_properties(SiriusLooper PROPERTIES
    XCODE_ATTRIBUTE_*)` block, active only under `-G Xcode`. The default
    `cmake -B build` (Unix Makefiles) stays ad-hoc-signed for fast dev
    iteration; `cmake -B build-xcode -G Xcode` produces a Developer-ID-
    signed bundle with hardened runtime.
  - Notarization gated behind `-DSIRIUS_NOTARIZE=ON`. Runs
    `xcrun notarytool submit --wait` → `xcrun stapler staple` → `spctl`
    verify as a post-build custom command.
  - One-time operator setup done: `xcrun notarytool store-credentials
    sirius-notary --apple-id itunes@larryseyer.com --team-id RR5DY39W4Q`.
    Profile validated against Apple's servers, stored in keychain.
  - Manual Xcode-UI verification on `Sirius Looper.app` showed
    `Authority=Developer ID Application: Larry Seyer (RR5DY39W4Q)`,
    `flags=0x10000(runtime)`, audio-input entitlement, timestamp.
- **OTTO backport — partial:** CMake block + entitlements file added to
  `OTTO/src/otto-plugin/CMakeLists.txt` and
  `OTTO/src/otto-plugin/OTTOPlugin.macos.entitlements` during this
  session. Another Claude is working OTTO concurrently — the edits are
  on disk but Sirius did not test the OTTO build end-to-end.
- **Deferrals from this session (DO NOT test claims unverified):**
  - `bash/smoke-persistence.sh` not run against the signed bundle
    (operator said "do not test"). The AppleScript -25211 should be
    cleared but is unverified.
  - Load dialog `.sirius.json` greying in `~/Downloads` is similarly
    unverified.
  - `SiriusTests` not re-run against the regenerated build trees;
    expected still 256 / 4283 (no code paths touched).

### 2026-05-16 — Developer ID signing milestone (original entry, for history)

- **Why this is its own session:** signing / notarization is a
  distinct skillset (Apple Developer account, keychain identity
  management, entitlements, hardened runtime, `codesign`, `notarytool`,
  stapling) — not a Sirius-engine task. It deserves a focused arc, not
  a side errand inside a feature session. Per the
  `feedback-defer-big-design-to-own-session` memory pattern.
- **What it unblocks (two symptoms, one root cause):**
  1. `bash/smoke-persistence.sh` — `tell process "Sirius Looper" to ...`
     fails with AppleScript error `-25211` because the bundle is
     ad-hoc-signed. The same call against `Finder` works from the same
     shell, so this is process-targeted denial, not a TCC scope issue.
     See the 2026-05-16 entry below.
  2. The Load dialog greys `*.sirius.json` files in `~/Downloads` — the
     four protected-folder TCC keys we added to `Info.plist` are inert
     against an ad-hoc bundle. See the 2026-05-15 entry below for the
     full failed-attempt log; the working hypothesis there reaches the
     same conclusion.
- **Current bundle signing state:**
  `codesign -dv "Sirius Looper.app"` reports
  `flags=0x20002(adhoc,linker-signed)`, `Signature=adhoc`, no
  entitlements. Below macOS's TCC trust threshold for both directions:
  the bundle cannot be granted protected-folder access, and other
  apps' System Events automation cannot target its processes.
- **What OTTO already provides (port these as-is):**
  Team ID `RR5DY39W4Q` (see memory
  `project-apple-developer-team-id`). OTTO's iOS targets use the
  CMake `set_target_properties()` pattern at
  `/Users/larryseyer/AudioDevelopment/OTTO/src/otto-ios/CMakeLists.txt`
  lines 258-272 — copy the structure, swap iOS specifics for macOS.
  The minimal entitlements file at
  `/Users/larryseyer/AudioDevelopment/OTTO/src/otto-ios/OTTO.entitlements`
  shows the format (Sirius's macOS keys will differ).
- **What OTTO does NOT have (genuinely new ground for both apps):**
  OTTO's *macOS desktop* targets (Standalone, VST3, AU, CLAP) are
  ad-hoc-signed exactly like Sirius today. No hardened runtime, no
  notarization, no `Developer ID Application` identity, no macOS
  entitlements file. Sirius's signing session is the first time
  desktop signing lands in this codebase family — the work is
  worth backporting to OTTO's macOS targets in the same arc.
- **What's needed to finish (sketch — actual session will tighten this):**
  1. **Verify Developer ID Application certificate is in the keychain.**
     `security find-identity -p codesigning -v` should list a
     `Developer ID Application: Larry Seyer (RR5DY39W4Q)` entry.
     If not, fetch from Apple Developer portal (cert exists implicitly
     since OTTO's iOS targets sign cleanly, but the macOS-specific
     `Developer ID Application` cert may be separate).
  2. **macOS entitlements file** at `app/SiriusLooper.macos.entitlements`
     containing at minimum:
       * `com.apple.security.files.user-selected.read-write` (fixes
         the Load dialog `.sirius.json` greying)
       * `com.apple.security.device.audio-input` (live capture path)
       * `com.apple.security.cs.allow-jit` only if a hosted plugin
         needs it (test without first; add only if a plugin scan fails)
  3. **CMake wiring in `app/CMakeLists.txt`** — extend the existing
     `juce_add_gui_app` / `PLIST_TO_MERGE` block with a
     `set_target_properties()` block modelled on OTTO's:
     ```cmake
     set_target_properties(SiriusLooper PROPERTIES
         XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "RR5DY39W4Q"
         XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
         XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
         XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
         XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
             "${CMAKE_CURRENT_SOURCE_DIR}/SiriusLooper.macos.entitlements"
         XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES")
     ```
     Gate behind `-DSIRIUS_SIGN=ON` so dev iteration stays cheap
     (ad-hoc default; signed on explicit opt-in and on the
     operator-verification build).
  4. **Notarization step** — post-build script that runs
     `notarytool submit ... --wait` and `stapler staple` only when
     `-DSIRIUS_SIGN=ON`. Needs an `xcrun notarytool store-credentials`
     keychain profile set up once; document the one-time setup in the
     session's deliverable.
  5. **Verification matrix** post-signing:
       * `codesign -dv "Sirius Looper.app"` reports
         `Authority=Developer ID Application: Larry Seyer (RR5DY39W4Q)`
         and the entitlements blob includes the keys above.
       * `spctl -a -t exec -vv "Sirius Looper.app"` reports
         `source=Notarized Developer ID`.
       * `bash/smoke-persistence.sh` exits 0 end-to-end (its TCC
         probe already validates the same trust transition).
       * Load dialog allows selection of `*.sirius.json` in
         `~/Downloads` (the 2026-05-15 entry's original symptom).
       * `SiriusTests` still green (headless tests don't touch the
         bundle but the CMake paths share configure logic).
  6. **Backport to OTTO** — once Sirius's macOS signing block is
     proven, replicate it in OTTO's `src/otto-standalone/CMakeLists.txt`
     so the sister apps stay in step. Reuse the same entitlements
     file location pattern; team ID and identity string are
     identical.
- **What this session should NOT do:** rewrite the persistence layer
  to work around signing, ship the directory-format work (separate
  entry below), or change anything about the operator-facing GUI. Pure
  bundle/distribution work.

### 2026-05-15 — DemoSession intro/outro violate Phrase-vs-Loop convention

- **Files:** `app/DemoSession.cpp` (lines ~46-80), possibly
  `tests/PreparationViewStateTests.cpp` and
  `tests/TimelineViewStateTests.cpp` if any view-state assertions
  depend on the current intro/outro shape.
- **What was deferred:** The capture-promotion design (see
  `docs/superpowers/specs/2026-05-15-capture-promotion-design.md` §1)
  and the user guide chapter 1 both teach the convention "a Loop is a
  leaf with `TapeReference`; a Phrase is a container with
  `PhraseMetadata`; a Loop is never standalone in the tree." The
  middle phrase in `DemoSession::buildDemoSession()` (the verse with
  two layered Loop children) honours this. **The intro and outro do
  not** — both attach `PhraseMetadata` *and* `TapeReference` to the
  same Constituent, making them Phrase-AND-Loop hybrids that fit
  neither category cleanly.
- **Why deferred:** Restructuring intro/outro into Phrase shells with
  single Loop children may touch view-state snapshot tests
  (`PerformanceViewStateTests`, `TimelineViewStateTests`,
  `PreparationViewStateTests`) and the `Regions:` diagnostic output
  could shift. Worth doing as a focused commit, not at the tail of
  the capture-promotion work session.
- **Operational consequence today:** capturing into intro or outro
  (Mark In with the playhead inside their span) lands a new Loop child
  inside a Constituent that already has its own `TapeReference`. The
  banner shows "Loop added to intro" — UX is fine; the data shape is
  the inconsistency. `promotion::promote` still works correctly.
- **What's needed to finish:**
  1. Rewrite intro and outro to be Phrase-only shells, each containing
     one `TapeReference`-bearing Loop child (mirror the verse's
     structure).
  2. Update any view-state test expectations that depend on the prior
     shape.
  3. Once green, the `findHostRecursive` walk in `core/src/Promotion.cpp`
     could optionally tighten to `isPhrase() && !tapeReference()` —
     the convention then becomes guarded by both the data and the
     code, not just the data.
- **Surfaced by:** final code review of capture-promotion (Important #1).

### 2026-05-15 — Shared-placement-with-per-instance-overlays architecture — SUPERSEDED-AND-IMPLEMENTED 2026-05-16

- **Status:** Implemented end-to-end 2026-05-16. The plan at
  `docs/superpowers/plans/2026-05-16-shared-placement.md` shipped
  across Sessions A + B + C (eight feature commits plus docs), all
  ten tasks done, all four operator gates verified, full suite at
  250 / 4269 assertions. Wrappers, shared-by-default capture,
  long-press = Overlay, "Vary this one" fork gesture, tie-bar /
  overlay-dot / prime-mark visuals, verse × 3 demo, pointer-aware
  runtime guard, and §11 banner copy all live on master. See
  `continue.md` for the full milestone arc and the three small
  code-review follow-ups still open (Shared-splice hoist,
  `value_or`→`jassert`, `refreshAll()` extraction).
- **Original spec/brainstorm:** preserved below for historical
  context; no longer actionable.

- **Files:** `core/include/sirius/Arrangement.h`,
  `core/src/Arrangement.cpp`, `core/include/sirius/Constituent.h`
  (possibly), `ui/src/TimelineViewState.cpp`,
  `ui/include/sirius/UndoStack.h`, `core/src/Promotion.cpp` (the
  runtime guard added by the capture-promotion design goes away),
  `docs/Sirius Looper User Guide.md` (Roadmap section + a new chapter
  once the feature lands).
- **What was deferred:** shared-placement semantics for repeated
  Phrases. Today `arrangement::sequence` (`core/src/Arrangement.cpp:60`)
  creates per-placement Constituent copies via `placedAt` — each
  placement is a distinct Constituent object that happens to share the
  same id. The user model requires shared-by-reference with
  per-instance overlay buckets so common layers (drums, bass, rhythm,
  harmony) propagate across all verse instances while differentiating
  layers (vocals, fills) attach to one placement only.
- **Why deferred:** load-bearing for repeated-section workflows
  (verse × 3, chorus × 4) but bigger than the capture-promotion
  brainstorm — touches Arrangement, possibly Constituent, the
  TimelineViewState selector, undo semantics across instances, and
  the renderer. Capture promotion ships single-instance-correct in
  the meantime; the runtime guard in `promotion::promote` ensures
  multi-instance cases throw loudly until this is settled. See
  `docs/superpowers/specs/2026-05-15-capture-promotion-design.md` §3
  for the guard.
- **What's needed to finish:**
    1. Settle Path B from the brainstorm: the arrangement layer
       becomes a sequence of `(Phrase ChildPtr, Position, optional
       overlay-children)` tuples. The Phrase ChildPtr is shared
       across placements.
    2. Design the per-instance overlay UX — where overlays attach in
       the data model, how the timeline distinguishes shared vs
       overlay rendering, whether overlays are themselves Phrase-shaped
       or a new struct.
    3. Design the "fork this placement into its own Phrase" gesture —
       the escape hatch for the rare "this verse is special" case.
    4. Decide undo semantics across instances: one undo entry =
       all-instances revert, or per-instance? Both have arguments.
    5. Extend `promotion::promote` to handle the multi-instance case
       — remove the runtime guard, propagate Loop adds across all
       Constituents matching the host id, handle overlay vs shared
       attachment based on operator gesture.
    6. Update `selectTimelineView` to render shared vs forked
       placements distinguishably.
- **Prerequisite for:** full multi-instance capture/promotion,
  Loop/Pill rendering for repeated phrases, the user-guide chapter on
  "Repeating song sections."

### 2026-05-16 — Hoist Shared-path splice out of `promote()` (code-review follow-up)

- **Files:** `core/src/Promotion.cpp` (the `promote()` body, currently
  ~226 lines, with the Shared-path splice at lines ~230–289 living as
  an inline `std::function` + recursive lambda).
- **What was deferred:** extracting the pointer-identity-preserving
  Shared splice into a private anonymous-namespace helper, e.g.
  `Constituent spliceLoopIntoSharedHost (root, hostPath, loopPtr)`.
  Also: add cross-reference comments between the Overlay path-based
  splice (lines ~187–197) and the Shared pointer-identity splice
  explaining why each shape was chosen (Overlay = one wrapper, path
  is enough; Shared = N references to one ChildPtr, must rewrite all
  occurrences).
- **Why deferred:** the shared-placement plan inlined the function
  verbatim; per the surgical-changes rule the implementation session
  shipped the plan as written plus a CLAUDE.md function-size
  justification comment (commit `b59a76e`). The hoist is a quality
  cleanup raised at code review (Important #2 in the Tasks 2+3
  review), not a correctness fix — it would drop `promote()` from
  ~226 to ~165 lines and give the helper its own focused docstring
  about the pointer-identity contract. Worth doing as a focused
  refactor commit, not bundled into Session B's UI work.
- **Operational consequence today:** none. Tests are green
  (244 / 4214 assertions); the behaviour is correct; the only cost
  is that a future maintainer reading the two splices side-by-side
  has to reverse-engineer why they're shaped differently.
- **What's needed to finish:**
    1. Extract the lambda + driver into a free helper in the
       `core/src/Promotion.cpp` anonymous namespace. Capture by
       parameter rather than by-reference closure.
    2. Add a one-line cross-reference comment at each splice site
       (Overlay → "see spliceLoopIntoSharedHost for the Shared
       analogue"; Shared → "see lines NNN for the Overlay analogue").
    3. Rename the inline lambda's `replaceShared` to something more
       descriptive (e.g., `rebuildSubtreeReplacingHost`) if the
       extraction approach keeps it as a lambda.
    4. Strengthen the existing pointer-equality comment to call out
       that `.get()` is deliberate vs `==` on `shared_ptr` (same
       semantics, but `.get()` reads as "raw pointer identity" which
       matches the contract).
    5. Verify full suite still green (244 cases) and the load-bearing
       pointer-equality assertions in
       `tests/PromotionTests.cpp::"promote with Shared and a wrapper
       covering Mark In adds the Loop to the shared Phrase"` still
       hold.
- **Surfaced by:** code-quality review of commit `d8a2479` (Tasks 2+3
  joint commit).

### 2026-05-16 — `announceCapture` Overlay branch: replace `value_or` fallbacks with `jassert`

- **Files:** `app/MainComponent.cpp` (the `announceCapture` Overlay
  branch in the §11 templates, around line 780).
- **What was deferred:** the Overlay branch uses
  `result.hostPhraseName.value_or("the phrase here")` and
  `result.overlayPlacementIndex.value_or(0u)`. Both fallbacks are
  unreachable by contract — when `resolvedMode == Overlay`, promote()
  guarantees both fields are populated (a wrapper was located and the
  placement ordinal computed). If the invariant ever broke, the banner
  would render `"Added to the phrase here 0 only"` — silently wrong
  copy that violates CLAUDE.md philosophy rule 8 ("fail loud").
- **What's needed to finish:**
  1. Replace `value_or` with direct dereference, gated by `jassert
     (result.hostPhraseName.has_value() && result.overlayPlacementIndex
     .has_value());` so debug builds catch contract regressions loudly.
  2. Re-verify the four banner scenarios still read correctly on
     operator gate.
- **Why deferred:** shipped Task 7 commit (`dd1c28c`) as the plan
  specified plus the dead-`wasDowngrade` deletion. The `value_or` ->
  `jassert` swap is a small follow-up that turns an unreachable-but-
  silent failure mode into an unreachable-but-loud one.
- **Operational consequence today:** none. The branch is unreachable
  in practice; tests + operator gate pass cleanly.
- **Surfaced by:** code-quality review of commit `dd1c28c` (Task 7).

### 2026-05-16 — Extract `MainComponent::refreshAll()` and use it everywhere

- **Files:** `app/MainComponent.h`, `app/MainComponent.cpp`.
- **What was deferred:** the four-line refresh sequence
  ```
  refreshPerformance();
  refreshPreparation();
  refreshCaptureControls();
  refreshDiagnostics();
  ```
  is duplicated at five call sites (`MainComponent.cpp:525-528`,
  `:938-941`, `:966-969`, `:1013-1016`, and partials at `:821-826`
  / `:1085-1086`). Task 8 (`38667d0`) was the fifth duplication.
- **What's needed to finish:**
  1. Add `void refreshAll();` to the private section of
     `MainComponent.h` with a one-line docstring.
  2. Define it in `MainComponent.cpp` as the four-call sequence.
  3. Replace each existing call site with a single `refreshAll();`
     invocation. Inspect partial-sequence sites (`:821-826` etc.) to
     decide whether they should also collapse to `refreshAll()` or
     stay partial.
  4. Verify the full suite still green (250 cases) and the operator
     gate scenarios still verify (capture → tie-bar, fork → prime
     mark, undo → restore).
- **Why deferred:** Task 8 was an "add the fifth copy" moment, not a
  "refactor four prior copies" moment. Code-quality reviewer flagged
  it as Important but Approved-with-follow-ups; the surgical-changes
  rule said to ship the plan-delta now and extract in a focused
  refactor commit.
- **Operational consequence today:** none. Every refresh fires
  correctly; the only cost is the file's growing repetition.
- **Surfaced by:** code-quality review of commit `38667d0` (Task 8).

### 2026-05-15 — OTTO Look-and-Feel integration (cross-app)

- **Files:** new top-level shared module (location TBD — see open
  question below), `ui/` (new `sirius::LookAndFeel` subclass),
  `app/Main.cpp` (currently calls `juce::Desktop::getInstance()
  .getDefaultLookAndFeel()` at line ~38 — replace with the shared
  Sirius L&F), CMake wiring on both OTTO and Sirius sides.
- **What was deferred:** wholesale visual integration with OTTO's
  existing design system. Sirius today runs JUCE stock `LookAndFeel_V4`
  with default colours and fonts; the user wants Sirius to visually
  match OTTO so the two products read as one family.
- **What OTTO already has (inventoried 2026-05-15):**
  Located at `/Users/larryseyer/AudioDevelopment/OTTO`.
  - **`src/otto-plugin/ui/OTTOColours.h`** (~240 lines):
    - Layered dark-theme palette: `bg0`..`bg6` (darker = further back,
      lighter = elevated). `bg0`/`bg1` are `0xff010000` (matches OTTO
      logo background — operator-locked); `bg2` panels; `bg3` modals;
      `bg4`/`bg5`/`bg6` interactive states.
    - Named borders, text tiers (`textPrimary` / `Secondary` /
      `Disabled` / `Inverse`).
    - Accent: `accent = #00d4aa` (teal), `accentBright`, `accentDim`.
    - **8 player colours**: coral, rose, gold, mint, orange, lavender,
      leaf, sky. These map naturally to Sirius's per-tape identity —
      the kind-colour band on TimelineView strip heads becomes a
      player-colour band.
    - Semantic state colors (success/warning/error), meter colors,
      transport colors (`playActive = #4ade80`, `stopActive = #f87171`),
      mute/solo/fill action colors, slider track/thumb, focus outline,
      menu highlight.
  - **`src/otto-plugin/ui/OTTOLookAndFeel.h/cpp`** (2154 lines):
    - Subclass of `juce::LookAndFeel_V4`.
    - Custom draw for: buttons (gradient backgrounds), text buttons,
      toggle buttons, tick boxes, linear sliders, rotary knobs (270°
      arc), labels (typography hierarchy by ComponentID), combo boxes,
      popup menus (radial-gradient backgrounds, section headers, scroll
      arrows), transport buttons (play/pause/stop), mute/solo buttons
      with hue, pattern grid cells (OTTO-specific), focus outlines.
    - Touch-first sizing — 44pt minimums (Apple HIG), HiDPI snap-to-
      pixel helpers, 4px spacing grid, named border radii.
    - Audio-pro section title convention: ALL CAPS, BOLD, CENTERED —
      FabFilter/UAD/Waves visual language. Helpers
      `applySectionTitleStyle(juce::Label&, text)` and
      `drawSectionTitle(g, bounds, text)` are the single source.
    - Roboto Condensed-based menu design tokens (`getMenuItemFont()`
      etc.) — JUCE's PopupMenu and an OTTO-internal touch popup share
      one tokenset.
  - **`assets/Fonts/`**: 10 families bundled as TTF binary-data:
    Roboto, Roboto Condensed, Orbitron, Bricolage Grotesque (5
    weights), JetBrains Mono (3 weights), Montserrat, Open Sans,
    Phosphor (icons), Playfair Display.
- **Decision made this session:** **shared-submodule model.**
  User picked this from a 4-option list (shared submodule / vendor
  copy / extract subset / re-implement-native). Rationale: sister apps
  that always ship together but sold individually — drift between
  their visual identities is a permanent tax; one source of truth is
  worth the one-time extraction cost.
- **Open question (next session must decide first):** *where the
  shared module physically lives.* Four candidates were ready to
  present:
  1. **New top-level repo** (e.g.
     `~/AudioDevelopment/audio-ui-core/`). Cleanest separation; both
     OTTO and Sirius pull via git submodule or CMake FetchContent.
     One-time cost: extract code from OTTO into the new repo, update
     OTTO to consume from it, add the consumer to Sirius. **Working
     recommendation** — most-aligned with the "shared submodule"
     answer.
  2. **OTTO's `ui/` becomes the canonical home**, Sirius adds OTTO as
     a submodule. Faster bootstrap but couples Sirius's CMake to all
     of OTTO (HISE assets, sampler code) and OTTO becomes a hard
     dependency for any Sirius build.
  3. **Sirius hosts** the shared module, OTTO consumes. Unusual since
     OTTO has the source today.
  4. **Pragmatic two-stage**: vendor what Sirius needs from OTTO now
     (commit as `ui/sirius/lookandfeel/` inside Sirius), and schedule
     the extraction-into-shared-module as a follow-up cross-repo
     session. Keeps Sirius UI velocity high; pays the refactor cost
     once both apps are visually settled. **Pragmatic fallback** if
     full shared-module setup is too much for one session.
- **Why deferred:** wholesale L&F adoption + cross-repo submodule
  setup is its own session-sized piece of work. The current session
  is TimelineView operator verification + small polish items; mixing
  the two bloats context.
- **What's needed to finish:**
  1. **Pick a module home** from the four options above. Working
     recommendation: option 1 (new top-level repo). Pragmatic fallback:
     option 4 (vendor first, extract later).
  2. **If option 1:** create the new repo, extract `OTTOColours.h`,
     `OTTOLookAndFeel.h/cpp` minus OTTO-specific widgets (pattern
     cells, player badges), plus the font binary-data infrastructure,
     into it. Rename namespace from `otto::` to whatever the shared
     namespace is (`audiodev::ui::`? `larryseyer::ui::`? — open).
     Update OTTO's CMake to consume from the shared module. Add the
     shared module as a submodule to Sirius. Bind a `sirius::LookAndFeel`
     subclass that extends the shared base with any Sirius-specific
     widgets (timeline pills, tape strips). Install it in
     `app/Main.cpp`.
  3. **If option 4:** copy `OTTOColours.h` and a stripped
     `OTTOLookAndFeel.h/cpp` (no pattern cells, no player badges) into
     `ui/include/sirius/` and `ui/src/`, rename `otto::` to `sirius::`,
     copy the relevant fonts from `OTTO/assets/Fonts/` to
     `Resources/Fonts/`, wire them as JUCE binary-data, install the
     L&F in `app/Main.cpp`. File a follow-up todo for the shared-module
     extraction.
  4. **Apply the L&F to TimelineView.** The strip-head kind-colour
     band becomes one of OTTO's 8 player colours (per-tape identity).
     Pills use the OTTO accent (`#00d4aa`) for the loop-on indicator,
     the membership-outline uses `borderStrong`. Section title
     convention (ALL CAPS BOLD CENTERED) applies to tab labels.
  5. **Apply to the bottom bar.** Transport buttons (Arm / Mark In /
     Mark Out / Undo / Redo) use OTTO's `drawButtonBackground` +
     mute/solo/transport hue conventions — Arm shows red when armed
     (mute family), Mark In/Out are accent-coloured action buttons.
- **Open sub-decisions for the next session:**
  - **Font subset.** OTTO ships 10 families; Sirius likely needs only
    3–4 (Roboto for body, Orbitron for display, Roboto Condensed for
    menus, JetBrains Mono for time/numeric readouts on the diagnostics
    row). Pick which to bundle to keep the .app bundle small.
  - **Shared namespace name.** Going from `otto::Colours::` to
    `audiodev::ui::Colours::`? `larryseyer::ui::`? `aui::`? Naming
    affects every call site in OTTO when the rename lands.
  - **Player-colour → tape-id mapping.** OTTO has 8 player colours;
    Sirius's demo uses tape ids 100/200/300/400. Need a stable
    `getPlayerColour(tapeId)` mapping (modulo 8? a registered
    per-tape colour stored in `InputDescriptor`?). The latter is
    nicer for performer agency (operator picks the colour) but
    larger scope.
  - **Touch targets.** OTTO is touch-first (44pt min, iOS in scope).
    Sirius is touch-friendly-on-iPad but desktop-primary for now.
    Adopt OTTO's 44pt minimums anyway (no harm on desktop), or trim
    to denser desktop sizing for Sirius? Working recommendation: keep
    44pt — the eyes-free performance metaphor (white paper 14.4)
    benefits from big targets even on desktop.
- **Headless verification implications:** none of this changes the
  testable `*ViewState` selectors — they remain JUCE-free. The L&F
  affects only the renderer half (`TimelineView`, `PerformanceView`,
  `PreparationView`, `MainComponent`). All 226 existing tests should
  pass unchanged across the L&F swap.

### 2026-05-15 — M5: Plugin scanner crashes + scan-strategy redesign

- **Files:** `host/src/PluginScanner.cpp`, `host/include/sirius/PluginScanner.h`,
  eventually the app-level integration that triggers scans and the
  persistence layer that caches scan results.
- **Symptom (operator report, 2026-05-15):** the standalone app crashed
  during a plugin-folder scan. Scan was taking the "usual LONG time" —
  same UX problem Logic Pro exhibits when a system has thousands of
  plugins installed. No crash log captured yet.
- **What was deferred:** treating this as a session-of-its-own design
  problem rather than wedging it into the current TimelineView-polish
  session. Three intertwined pieces:
  1. **Crash protection.** A scan that ploughs through every VST3/AU on
     disk inevitably encounters at least one plugin whose constructor
     throws, deadlocks, or segfaults. JUCE's `AudioPluginFormatManager`
     instantiates plugins in-process; a bad plugin crashes the host.
     The standard mitigation is out-of-process scanning — a small
     child-process probe per plugin, with the parent timing it out and
     marking the plugin as failed if the child dies. JUCE's
     `KnownPluginList::scanAndAddDragAndDroppedFiles` and
     `AudioPluginFormat::createPluginInstanceFromDescription` both have
     async overloads; the child-process pattern is documented in
     JUCE's `PluginListComponent` source.
  2. **Light scan mode (file metadata only, no instantiation).** For
     users with thousands of plugins, deep-scanning every one on first
     launch is hostile. A "light" scan reads only what the OS can
     cheaply tell us — bundle name, format, manufacturer string from
     the Info.plist (AU) or `moduleinfo.json` (VST3 3.7+) — and defers
     full instantiation (which is what discovers parameter counts,
     factory presets, etc.) until the user actually drops the plugin
     into an effect chain.
  3. **Lazy-instantiate-on-first-use ("scan when loaded").** The
     operator's "outside the box" idea: skip the upfront full-scan
     entirely. Show every plugin the OS knows about (from the standard
     paths) immediately; pay the per-plugin instantiation cost the
     first time the user *uses* that plugin, and cache the result.
     This is what Bitwig does. Plays well with light scan (#2) — light
     scan populates the visible list, full scan happens on demand and
     persists.
- **Why deferred:** the current session is TimelineView operator
  verification + small UI polish. Plugin-scanner redesign is its own
  design problem (cache schema, child-process protocol, UI for
  failed-plugin reporting, persistence-format migration since
  `PluginScanner::Result` shape will change). Worth a dedicated session
  with a brainstorm pass and a written design before code.
- **What's needed to finish:**
  1. **Capture a crash report.** Next time the scanner crashes, grab
     the `~/Library/Logs/DiagnosticReports/Sirius Looper-*.crash` file
     and attach it to this entry — that determines whether the crash
     is in our scanner code, in JUCE's plugin loader, or in a specific
     plugin's constructor.
  2. **Design session (separate brainstorm):** decide between
     out-of-process child scanning vs. an in-process try/catch +
     watchdog timer, and decide whether to ship light-scan + lazy-load
     together or sequentially.
  3. **Implement crash protection first** — even an in-process
     try/catch around `createPluginInstance` plus a per-plugin timeout
     would have caught today's crash. Out-of-process can come later;
     the immediate value is "scan completes even when a plugin
     misbehaves."
  4. **Then implement light scan as the default**, with a "deep scan
     this one" gesture on the plugin row when the user wants the full
     parameter list before loading.
  5. **Lazy-instantiate-on-first-use** is the destination architecture;
     ship it once the cache + UI surfaces from steps 3-4 exist.
- **Headless verification already done for M5:** scanner
  format-registration, descriptor structures, GenericParameterView
  construction against a JUCE `AudioProcessor`. The instantiation /
  scan-runtime path is the operator-deferred half and is precisely
  where today's crash lives.

### 2026-05-14 — M0: Project skeleton & CI

- **Files:** n/a (external/operator actions)
- **What was deferred:**
  1. Throwaway FFmpeg-integration spike to de-risk M6 video.
  2. Operator verification that the standalone window launches and renders
     (build produces a valid `Sirius Looper.app` bundle; the window itself was
     not launched in this environment).
  3. CI workflow (`.github/workflows/ci.yml`) is committed but unverified — it
     cannot run until the repo has a GitHub remote and a push.
- **Why deferred:**
  1. Exploratory; requires FFmpeg installed locally and is M6-scoped risk
     reduction, not M0-blocking.
  2. GUI testing is operator-run per project conventions.
  3. No remote configured; pushing is an explicit operator decision.
- **What's needed to finish:**
  1. Install FFmpeg, write a small decode-one-frame probe, confirm libav links
     cleanly via CMake on all three platforms. Do before/early in M6.
  2. Launch `build/app/SiriusLooper_artefacts/Release/Sirius Looper.app` and
     confirm the window opens, is resizable, and shows "Sirius Looper".
  3. Add a GitHub remote and push; confirm the CI matrix goes green on
     macOS/Windows/Linux.

**Resolved:** Ableton Link license procurement — the proprietary license is
already held (same licensing model as the sister app OTTO; see
`docs/LICENSE-THIRD-PARTY.md`). No procurement action is outstanding.

### 2026-05-14 — M2: Membranes & always-running tape

- **Files:** future `engine/` membrane classes + `app/AudioDeviceManagement.*`
- **What was deferred (the hardware-dependent half of M2):**
  1. JUCE `AudioIODeviceCallback` device wiring — the thin glue that drives the
     inbound membrane (timestamp + enqueue to the lock-free queue) and the
     outbound membrane (render the loop) from a real audio device.
  2. One-time loopback latency calibration per device (output a click, capture
     it back, measure the round-trip).
  3. The end-to-end "audio in → tape → mark a loop → hear it repeat" milestone
     test.
- **Why deferred:** all three require real audio hardware; device and audio
  testing is operator-run per project conventions, not verifiable headless.
- **What's needed to finish:**
  1. Build the membrane device-callback classes on top of the verified engine
     foundation (lock-free queue, LMC, membrane math, LoopRenderer, Asrc), then
     wire them through JUCE's `AudioDeviceManager` in `app/`.
  2. Run the loopback calibration on a real interface; confirm it populates an
     `AudioDeviceCalibration` with sane rate-factor/offset values.
  3. With an instrument plugged in: capture audio, mark a loop, confirm it
     repeats audibly and stays in time over many cycles.

- **Headless verification already done for M2:** lock-free SPSC queue
  (incl. 200k-item concurrent stress), retroactive ring, LMC, latency-
  compensation math, device calibration, LoopRenderer, and the libsoxr ASRC —
  78 tests pass. The libsoxr variable-rate latency measurement the plan asked
  for: reported delay ~2.1 ms, impulse-response latency ~0.04 ms — both
  comfortably inside the <30 ms trust budget.

### 2026-05-14 — M3: Minimal functional UI

- **Files:** `app/SessionInspector.{h,cpp}`, `app/DemoSession.{h,cpp}`,
  `app/Main.cpp`
- **What was deferred:**
  1. Operator verification that the session-inspector window launches: the
     Constituent tree renders as an indented hierarchy, the playhead slider
     scrubs, and the "loops sounding" panel updates as it moves.
- **Why deferred:** GUI testing is operator-run per project conventions; the
  build produces a valid `Sirius Looper.app` bundle but the window was not
  launched in this environment.
- **What's needed to finish:**
  1. Launch `build/app/SiriusLooper_artefacts/Release/Sirius Looper.app`.
     Confirm: the demo tree shows the session with intro/verse/outro phrases
     (verse holding two layered loops); dragging the playhead from 0 to 24 s
     moves the active-reads panel through intro -> verse (2 loops) -> outro;
     the read positions advance monotonically within each phrase.

- **Headless verification already done for M3 UI:** the inspector is built on
  the same RenderPipeline exercised by 105 passing unit tests (incl. the
  arrangement integration test that pins down sequenced end-to-end playback);
  the demo tree is constructed with the verified `arrangement::sequence` /
  `arrangement::layer` primitives. The UI is a pure view over verified core +
  engine code — no new untested logic, only JUCE rendering of it.

### 2026-05-14 — M5: Plugin hosting milestone test

- **Files:** `host/PluginScanner.{h,cpp}`, `host/GenericParameterView.{h,cpp}`,
  and the app integration that hosts a scanned plugin's processor and shows
  its parameter view.
- **What was deferred:**
  1. The plan's milestone-test commitment: "scan and instantiate at least one
     real plugin of each supported format (VST3 on every platform, AudioUnit
     on macOS); confirm parameter automation round-trips through a parameter
     tape."
  2. Wiring the GenericParameterView into the app's SessionInspector (or a
     successor panel) so the operator can actually drive a hosted plugin and
     see/edit its parameters.
  3. Optional CLAP and AUv3 hosting — explicitly gated as best-effort by the
     plan; not in any milestone gate.
- **Why deferred:** every part requires real plugin binaries installed on the
  test machine and a real-audio path; per project conventions hardware/GUI
  testing is operator-run.
- **What's needed to finish:**
  1. Have real plugins installed (at least one VST3; on macOS at least one
     AudioUnit). Run a PluginScanner, confirm both formats appear in
     `descriptors` with sensible names/manufacturers and no entries in
     `failedFiles`.
  2. Instantiate one via JUCE's `AudioPluginFormatManager::createPluginInstance`
     using the descriptor's `uniqueId` and `filePath`, attach the resulting
     `AudioProcessor` to a GenericParameterView in a small host harness,
     confirm sliders match the plugin's parameter count and respond to drag.
  3. With the plugin running through the audio path, move a parameter slider
     while recording onto a `Tape<ParameterEvent>` and play back; confirm the
     replayed events drive the plugin parameter and the view reflects the
     replay.

- **Headless verification already done for M5:** PluginDescriptor and
  EffectChain (copy-on-write, persistence round-trip, exhaustive variant
  coverage), ParameterEvent (range invariant, tape append-forward, the
  Constituent-over-parameter-tape recursion), PluginScanner's format
  registration (VST3 always; AudioUnit on macOS), and GenericParameterView's
  construction against a JUCE `AudioProcessor` — 146 tests pass. The
  hosted-plugin runtime that connects them is the operator-verified half.

### 2026-05-14 — M7: The performer's instrument — operator verification

- **Files:** `ui/PerformanceView.{h,cpp}`, `ui/PreparationView.{h,cpp}`, the
  app integration that drives them.
- **What was deferred:**
  1. Wiring `PerformanceView` and `PreparationView` into the standalone app
     in place of `SessionInspector`, and switching between cognitive modes
     (Performance vs Preparation, white paper 14.4) with a single gesture.
  2. Driving `UndoStack` with real edit operations — bind a global
     keyboard/footswitch undo and redo, plumb it through every editing path,
     and confirm a multi-step session of edits undoes and redoes cleanly.
  3. Integrating `LatencyBudget` with the UI's frame-loop and announcing
     when the rolling window falls below the budget (white paper 13.3
     rule 3, 14.8). The budget is measurable; the announcement surface
     belongs to the running app.
- **Why deferred:** every part requires the operator at the screen and a
  real gesture loop — eyes-free operation, glance tests, and trust-budget
  feel are all human-perceptual qualities.
- **What's needed to finish:**
  1. Add a Performance/Preparation toggle to the main window; route the
     same `Constituent` root to `selectPerformanceView` and
     `selectPreparationView` on every update.
  2. Hook the undo stack into every Constituent-producing edit path, push
     a label per edit, and confirm undo/redo land on the right snapshots.
  3. Measure UI-event-to-paint latency, feed each frame to
     `LatencyBudget`, and surface the rolling `fractionWithinBudget` in
     the Preparation view's diagnostics row.

- **Headless verification already done for M7:** UndoStack (push/undo/redo,
  redo-branch invalidation, depth cap, null guards), LatencyBudget (band
  thresholds, rolling window, invalid input rejected), the Performance and
  Preparation view-state selectors (deepest named container wins as
  foreground phrase, cycle-status formatting for the three cardinalities,
  full depth-first row enumeration with effect-chain and role-fillable
  flags) — 163 tests pass. The JUCE Components are thin renderers over
  those verified state structs.

### 2026-05-14 — M8: Ensemble — operator verification

- **Files:** `net/LmcElection.{h,cpp}`, `net/SessionMerge.{h,cpp}`,
  `net/Transport.h`, plus the (currently unwritten) network transport that
  carries the message types over real sockets.
- **What was deferred:**
  1. The plan's milestone test: "two-node partition-and-rejoin test
     confirming clean CRDT merge with no audio loss" — requires two real
     machines, a real network, and a real way to induce and lift a
     partition.
  2. The transport implementation itself: real sockets, frame format,
     reliability, discovery, monitoring previews. None of this exists yet;
     the message data types are written and tested but no wire goes
     anywhere.
  3. Per-node interval *measurement* feeding `LmcElection`. The election
     consumes intervals; the platform-specific code that *produces* an
     interval from each node's clock-discipline source (GPS, NTP, audio
     word clock, quartz, software estimate) is the operator-side
     contribution.
- **Why deferred:** every part requires real networking and real clocks.
  Per project conventions, hardware-dependent and network-dependent work
  is operator-run.
- **What's needed to finish:**
  1. Pick a transport (the plan does not commit to one; OSC over UDP and
     Ableton Link's own discovery layer are plausible candidates).
     Implement send/receive against `EnsembleMessage` with a small
     reliable-multicast or per-message-type semantics.
  2. Wire each node's clock-discipline source through to a
     `NodeClockEstimate`. Run `electLmc` on every topology change. Honour
     the result — slaves discipline against the master's broadcast time
     announcements.
  3. Use `MergeableSession::merge` on rejoin. The two-node milestone test:
     start with a shared session, partition the network, have each node
     edit different (and the same) Constituents, rejoin, merge, confirm
     no audio loss and no rewritten tape data.

- **Headless verification already done for M8:** LmcElection (anchor
  override beats tier, tier dominance beats numerical majority, Marzullo
  discards falsetickers and picks the narrowest-interval master),
  SessionMerge (tape-hash union, version union, commutativity,
  associativity, idempotence, last-writer-wins on the active version), and
  the `EnsembleMessage` variant (each kind constructs and pattern-matches
  cleanly) — 178 tests pass total across the whole project.

### 2026-05-14 — M6: Video — operator verification

- **Files:** `video/VideoFrame.h`, `video/VideoTape.h`,
  `video/FrameMembrane.{h,cpp}`, `video/VideoPreview.{h,cpp}`, and the
  (currently unwritten) FFmpeg decode/encode pipeline that produces and
  consumes the bytes the data model stores.
- **What was deferred:**
  1. The FFmpeg spike still owed from M0: install FFmpeg locally, write a
     small decode-one-frame probe, confirm libav* links cleanly via CMake
     on macOS, Windows, and Linux. **M6's runtime cannot proceed without
     this** — the data model is complete; the bytes are missing.
  2. The decode pipeline that fills `VideoFrame::pixels` from a real video
     source (camera input, file playback) and writes the metadata the tape
     and membrane consume. Real plumbing — choose libav* directly or wrap
     it behind a small abstraction layer.
  3. The encode pipeline that takes a captured `VideoTape` and writes it
     to disk in an intra-frame codec (white paper Part 6.5: roughly half
     the storage cost of uncompressed, decode cost paid at read).
  4. The conversion from a `VideoFrame` payload (any of the five pixel
     formats) to a `juce::Image` for `VideoPreview::setFrame`. swscale or
     equivalent does this trivially once linked.
  5. The plan's milestone test: "frame-accurate playback test against a
     known video file; confirm audio/video stay LMC-locked over a
     multi-minute render."
- **Why deferred:** every part requires FFmpeg installed locally and real
  source material. Per project conventions, anything with a real
  hardware/codec dependency is operator-run.
- **What's needed to finish:**
  1. Run the FFmpeg spike. Confirm linkage on the three platforms; pick a
     decode entry point (probably `avformat_open_input` +
     `avcodec_send_packet` / `avcodec_receive_frame`) and a swscale path
     for pixel-format conversion to the five `VideoPixelFormat` values
     the data model commits to.
  2. Wire decoded frames onto a `VideoTape`: each `av_frame` becomes a
     `VideoFrame` with width/height/format from the av_frame and a
     `presentationLmcSeconds` computed from the av_frame's pts and the
     stream's time_base. Append in order; the tape's
     "non-decreasing LMC time" rule enforces correctness.
  3. Drive `VideoPreview` from the tape: every animation tick, call
     `findFrameAt(tape, currentLmcTime)`, convert the returned frame's
     bytes to a `juce::Image` via swscale, hand to
     `VideoPreview::setFrame`.
  4. Validate `FrameMembrane` against real frame-rate-mismatched content:
     a 24 fps clip played at 30 fps display should stutter on the
     repeated frame; a 30 fps clip played at 24 fps display should skip
     the dropped frame. The math is already tested; this confirms the
     end-to-end pipeline preserves it.
  5. Run the multi-minute audio/video LMC-lock test the plan asks for —
     the membrane has the math; the runtime needs to honour it across a
     long playback.

- **Headless verification already done for M6:** the VideoFrame data
  model (metadata + opaque bytes), `Tape<VideoFrame>` (same shared
  template every Sirius tape uses), `findFrameAt` (the most-recent-frame-
  at-or-before-query rule, with the empty-tape, before-first, on-frame,
  between-frames, and past-end cases pinned down), and `FrameMembrane` /
  `convertFrameRate` (exact-Rational nearest-frame selection, including
  the awkward broadcast rates 23.976 / 29.97 / 59.94 staying exact, the
  24→30 stuffing pattern, the 30→24 dropping pattern, and the offset-
  start case) — 191 tests pass total. The `VideoPreview` JUCE component
  is a thin letterboxing renderer that takes a `juce::Image`; producing
  that image from a `VideoFrame` is the FFmpeg-bound work above.

### 2026-05-15 — Session directory format (Whitepaper V2 §7.8)

- **Update 2026-05-16:** `session.json` now serializes shared
  placements correctly (format bumped to v2, commit `a1b6ed3`). The
  remaining work in this entry is the *directory wrapper*: bundling
  LMC discipline history, per-device calibration, and the TapeStore-
  resident audio into the same `.sirius/` archival unit. The
  shared-encoding concern that previously blocked this entry is
  resolved.
- **Files:** `persistence/src/SessionFormat.cpp`,
  `persistence/include/sirius/SessionFormat.h`, `app/MainComponent.cpp`
  (Save/Load callers), eventually `persistence/src/TapeStore.cpp`.
- **What was deferred:** Whitepaper V2 §7.8 says a session is a
  directory `my-session.sirius/` containing:
  ```
  session.json            # the Constituent graph
  lmc-discipline.json     # LMC discipline history (Part 4.5)
  calibration/            # per-device latency + clock calibration
      <device-id>.json
  tapes/                  # the data layer — content-addressed
      <tape-id-hash>.{caf,flac,mkv}
  ```
  Current code writes a single `session.sirius.json` file containing
  only the Constituent graph. LMC discipline history and per-device
  calibration are not persisted; `TapeStore` exists but is not
  bundled into the same session unit. A session today is not the
  self-contained "valid archival unit" §7.8 describes.
- **Why deferred:** the plan's "Remaining open item" (line 273-277)
  was to *check* the V2 worked example against M1 structs — that
  check is done in commit (this commit). The actual file-format
  refactor is a separate scope decision: it touches Save/Load
  (which still has the unresolved macOS Load dialog TCC bug, see
  next entry), and would change every saved session's on-disk shape.
- **What's needed to finish:**
  1. Add a `Session` aggregate type to `persistence/` that bundles
     {root Constituent, optional LMC discipline history, optional
     calibration records, references to the TapeStore-resident
     audio data}. `SessionFormat::serialize/deserialize` operate on
     `Session`, not bare Constituent.
  2. Change Save to write a directory: create
     `<chosen-path>/session.json`, `<chosen-path>/calibration/`, and
     a `<chosen-path>/tapes/` symlink or copy from the live
     TapeStore. JUCE's `File::createDirectory()` plus the existing
     JSON serialization are sufficient.
  3. Change Load to read a directory: detect the `.sirius/`
     directory, parse the constituent JSON, materialize the
     calibration records, attach the bundled `tapes/` to the
     runtime TapeStore (likely via content-address verification:
     hashes inside must match filenames).
  4. Resolve the macOS Load dialog TCC bug first — switching Load
     to "pick a directory" instead of "pick a file" may change
     which TCC failure mode we hit, and we should avoid stacking
     unknowns.
- **Headless verification already done:** the Whitepaper V2
  Appendix C consistency check is complete — every field used in
  C.1 (twelve-bar blues) and C.2 (4-against-7 polymetric phrase)
  is representable in the M1 structs. The V2 surface vocabulary
  ("Section", "Phrase", "Loop", "Slice") maps onto Constituent +
  the optional fields (`phraseMetadata`, `tapeReference`,
  `repetitionRules.cardinality`); the engine has no separate Slice
  type because §7.5 explicitly defines a slice as "a loop with
  cardinality = Once," and `RepetitionRules::defaultOneShot()`
  encodes that. The `is_role_fillable = false` on a Section in V2
  C.1 is V2 being expository — Sections do not carry
  PhraseMetadata, so they cannot be marked fillable, and the
  default is "not fillable" for everything outside a phrase.

### 2026-05-15 — Mark Out should announce the new region visibly — SUPERSEDED

- **Status:** Items 1 and 3 of this entry are superseded by the
  capture-promotion design at
  `docs/superpowers/specs/2026-05-15-capture-promotion-design.md`.
  Item 1 (transient on-screen confirmation) shipped earlier this
  session as the CaptureBanner. Item 3 (promotion) is the subject of
  the design doc. Item 2 (persistent capture-history widget) is
  superseded — auto-promotion makes captures Pills on the timeline,
  serving the same need as a history list. No further action on this
  entry; it is preserved for context only.

### 2026-05-16 — GUI smoke-test script — SUPERSEDED-AND-IMPLEMENTED 2026-05-16

- **Status:** Verified end-to-end this session. `bash/smoke-persistence.sh`
  round-trips Save / Load against a Developer-ID-signed bundle and
  exits 0 with refs=2 confirming v2 shared-encoding. Five AppleScript /
  Launch Services landmines worked around — see the Auto-testing
  milestone entry above for the diff catalogue. Commit `4e8a1df`.

### 2026-05-15 — Load dialog .sirius.json greying — RESOLVED-VIA-SIGNING 2026-05-16

- **Status:** the Load dialog now reliably accepts `.sirius.json` on
  the Developer-ID-signed bundle. Verified by `bash/smoke-persistence.sh`
  driving the actual NSOpenPanel and round-tripping a saved session.
  The remaining sub-question (does Finder *double-click* on a
  `.sirius.json` in `~/Downloads` open it in Sirius?) is unverified
  this session — that's a Launch Services type-association question
  separate from the dialog gating, and intersects with the open-app
  -10825 follow-up below.

- **Original entry (historical):**
- **Files:** `app/MainComponent.cpp` (`chooseFileAndLoad`),
  `app/CMakeLists.txt` (the `PLIST_TO_MERGE` TCC keys).
- **What was deferred:** the macOS NSOpenPanel greys out
  `session.sirius.json` (and `.md` files) in `~/Downloads` no matter what
  is tried. Save side writes the file fine; only Load is broken.
- **What was already attempted (all failed):**
  1. Filter `*.sirius.json;*.json` (commit `cad8cb9`).
  2. Filter `*.json` (commit `f9deccc`).
  3. Filter empty `juce::String()` (commit `c041936`). JUCE source review
     showed this is *worse* — when `filters.size()==0`, the
     `shouldEnableURL` delegate in
     `juce_FileChooser_mac.mm:279-288` falls through to a
     directory-only allow, so every non-directory greys out.
  4. Filter `"*"` (uncommitted). JUCE source says this should pass both
     `createAllowedTypesArray` (returns `nil`, no UTI restriction) and
     the delegate's `matchesWildcard` check, allowing every file. In
     practice the file was still greyed.
  5. Non-native dialog via the `useOSNativeDialogBox=false` constructor
     parameter (uncommitted). It worked but the UI is unacceptable and
     additionally triggered the macOS TCC prompt for Downloads — which
     is the diagnostic clue the bug isn't in the filter at all.
  6. Added the four protected-folder TCC keys
     (`NSDownloadsFolderUsageDescription`,
     `NSDocumentsFolderUsageDescription`,
     `NSDesktopFolderUsageDescription`,
     `NSRemovableVolumesUsageDescription`) to the Info.plist via
     JUCE's `PLIST_TO_MERGE`. Verified the keys land in
     `Contents/Info.plist`. Still greyed.
- **Working hypothesis:** the ad-hoc-signed bundle
  (`codesign -dv` reports `flags=0x20002(adhoc,linker-signed)`,
  `Signature=adhoc`, no entitlements) is not trusted enough by macOS
  to receive the TCC permission prompt at all, so the keys we added
  are present but inert. `tccutil reset` against the bundle ID does
  nothing because no TCC record exists. The combination of "no
  Developer ID signature + protected folder + ad-hoc bundle" appears
  to be the failure mode.
- **What's needed to finish:**
  1. Sign the bundle with a Developer ID Application certificate (or
     enable the hardened runtime + entitlements at minimum) so macOS
     issues a TCC prompt. Quickest test: `codesign --sign - --deep
     --force --entitlements <plist> "Sirius Looper.app"` with a
     minimal entitlements file containing
     `com.apple.security.files.user-selected.read-write`.
  2. Or: investigate why `Elephant.png` was selectable while
     `.sirius.json` / `.md` were greyed in the same panel. That
     asymmetry suggests macOS may be applying a file-type-class
     allowlist (images pass, opaque-data files don't) when the bundle
     is below the trust threshold — independent of the JUCE filter.
  3. Or: ship the load via a drag-and-drop target on the Preparation
     pane (`FileDragAndDropTarget`) as a secondary path. The user
     drags `session.sirius.json` from Finder onto the app window;
     bypasses NSOpenPanel entirely. Save stays as-is.
- **Out of scope until then:** continuing with the rest of the headless
  coding work. Returning to this bug after that is finished.
- **State left in tree:** `app/CMakeLists.txt` has the TCC keys
  committed; `app/MainComponent.cpp::chooseFileAndLoad` is restored to
  the simple `"*.json"` filter (matches the f9deccc baseline before the
  empty-filter experiment).

---

### 2026-05-15 — Marketing site asset gaps (siriuslooper.com)

The v0 website was scaffolded under `/website/` (Eleventy, deploys to
`gh-pages` via `.github/workflows/pages.yml`, custom domain
`siriuslooper.com`). The following placeholders shipped and need real
assets before public launch:

- **Logo / wordmark:** Currently a typographic mark in Orbitron with a
  teal accent on the "S". Replace with a commissioned mark or designed
  wordmark. Files to update: `website/src/_includes/base.njk` (header
  brand + footer brand markup), `website/src/assets/img/favicon.svg`.

- **App screenshots:** None on the site. Add once GUI is past operator
  verification for milestone M3+ (TimelineView + CaptureBanner +
  four-tab UI). Drop captures into `website/src/assets/img/screens/`
  and wire into `index.njk` and `features.njk`.

- **Demo video:** None on the site. Record once retroactive capture
  and polymetric phrases are demoable end-to-end. Embed in `index.njk`
  hero (replace the SVG tape illustration) or `features.njk`.

- **Open Graph card:** Site references `/assets/img/og.png` in
  `base.njk` meta tags; file does not exist. Generate a 1200×630 PNG
  with the wordmark on `#0a0a0a` once the logo lands.

- **Self-hosted webfonts:** `base.njk` currently loads Orbitron,
  Inter, and JetBrains Mono from Google Fonts. Per the original plan,
  swap to woff2 subsets under `website/src/assets/fonts/` and
  `@font-face` declarations in `site.css` for speed and deploy
  stability.

- **Email signup endpoint:** `website/src/_data/site.json#signupEndpoint`
  is empty. Pick a provider (ConvertKit recommended for proper
  double-opt-in newsletter; Formspree for a simple inbox), paste the
  form-action URL into `signupEndpoint`, redeploy.

- **GitHub repo URL verification:** Site assumes
  `https://github.com/larryseyer/SiriusLooper` (matches the
  user/slug convention used for OTTO). If the parallel push session
  used a different repo name, update `website/src/_data/site.json#github`
  and the `View source on GitHub` link in `doc.njk`.

- **Custom domain DNS + Pages config (operator step):** Add A records
  to GitHub's apex-domain IPs (185.199.108–111.153) at the registrar
  for `siriuslooper.com`, then in repo Settings → Pages set source =
  `gh-pages` branch, custom domain = `siriuslooper.com`, Enforce HTTPS
  on. Verify with `dig siriuslooper.com +short`.

### 2026-05-18 - M7 S7 testing-only PluginScanner hardcoded "FabFilter" filter
- Files: host/src/PluginScanner.cpp (kTestingScanFilter constant + scanOneFormat body)
- What was deferred: the scanner currently skips every plug-in file whose path does not contain "FabFilter". This was a temporary measure so the operator (1000+ installed plug-ins) could eyes-on the M7 S7 "Open editor" workflow without waiting minutes for a full scan.
- Why deferred: the operator asked for the filter for the S7 eyes-on session only; replacing PluginDirectoryScanner with a manual loop also lost the failed-files reporting surface.
- What's needed to finish: (1) remove the hardcoded "FabFilter" substring or replace it with an opt-in mechanism (env var, UI text field, scan-on-demand-per-vendor); (2) restore failed-file reporting (either re-add PluginDirectoryScanner alongside the filter, or wrap scanAndAddFile's bool return value to surface failures).

### 2026-05-18 - M7 S7 eyes-on found: XPC bridge unreachable from .app context — SUPERSEDED 2026-05-18 by M7 S8 (next entry)

(See next entry. M7 S8 fully diagnosed this; the engine path now succeeds and the child path is blocked by an architectural design flaw deferred to a separate session.)

### 2026-05-18 — M7 S8 partial-win: engine→bridge handshake landed; child→bridge needs new architecture

- **Files (already updated this session, on `origin/master` once committed):**
  - `xpc_service/main.cpp` (os_log instrumentation throughout)
  - `host/src/PluginGuiBridge.cpp` (os_log + switched to `xpc_connection_create`)
  - `host_process/main.cpp` (os_log + switched to `xpc_connection_create`)
  - `xpc_service/CMakeLists.txt` (Developer-ID POST_BUILD re-sign of .xpc)
  - `app/CMakeLists.txt` (Developer-ID POST_BUILD re-sign of sirius_plugin_host + final outer .app re-sign)

- **What landed (engine side is GREEN end-to-end):**
  1. **Diagnostic instrumentation** — full `os_log` trace via subsystem
     `com.larryseyer.siriuslooper`, category `gui-bridge`. Filter with
     `log show --predicate 'subsystem == "com.larryseyer.siriuslooper"' --last 5m`.
     Silent XPC failures are the failure mode this session burned a
     full chat diagnosing; the logging stays permanently.
  2. **Codesign identifier matching** — the .xpc bundle now has
     codesign `Identifier=com.larryseyer.siriuslooper.gui-bridge` to
     match the plist's `CFBundleIdentifier`. Was `sirius_gui_bridge`
     (the binary name) because `--sign -` (ad-hoc) defaults to that.
     launchd refuses to register in-app XPC services when the two
     differ.
  3. **Developer-ID re-sign of embedded helpers** — macOS 14+ AMFI
     rejects ad-hoc-signed embedded XPC services with
     `Code=-423 "The file is adhoc signed or signed by an unknown
     certificate chain"`. POST_BUILD now re-signs the .xpc, the
     `sirius_plugin_host` helper, and the outer .app with
     `Developer ID Application: Larry Seyer (RR5DY39W4Q)` plus
     `--options runtime`. `codesign --verify --deep --strict` passes.
  4. **Engine uses `xpc_connection_create`, not
     `xpc_connection_create_mach_service`** — the latter looks up in
     launchd's Mach service namespace (gui/$UID domain), where in-app
     XPC services in `Contents/XPCServices/` are NOT visible. The
     former resolves the bundle id against the calling process's own
     bundle. Symptom of the wrong API: launchd logs `failed lookup:
     name=... error=3: No such process`.
  5. **Final outer .app re-sign closes M7 S6 deviation #2** — the
     parent's CodeResources seal now covers `Contents/XPCServices/`
     and the embedded `sirius_plugin_host`. (Note: NOT `--deep`, which
     would clobber the .xpc's identifier with the parent's.)

  Verified by os_log trace at 21:59:32 (2026-05-18):
  ```
  engine: XpcGuiBridge ctor
  engine: serverPort=100639
  engine: sending set_server_port
  sirius_gui_bridge: xpc_main starting
  service: new peer connection
  service: op=set_server_port
  engine: set_server_port ok; ready_=true
  ```

- **What's still broken (architectural, deferred to its own session):**
  The child (`sirius_plugin_host`) calls
  `xpc_connection_create("com.larryseyer.siriuslooper.gui-bridge", ...)`
  and gets routed to a **DIFFERENT bridge process** with empty state.
  launchd spawns one bridge per requester:
  ```
  [pid/<engine_pid>/com.larryseyer.siriuslooper.gui-bridge [37633]: Successfully spawned
  [pid/<child_pid>/com.larryseyer.siriuslooper.gui-bridge [37635]: Successfully spawned
  ```
  The cached `g_cachedServerPort` is process-local to each bridge
  instance. The child's `get_server_port` against its own (fresh)
  bridge returns no port.

  Per Apple's design, **in-app XPC services in `Contents/XPCServices/`
  are PRIVATE to each calling process's bundle scope.** They cannot
  serve as a shared broker across unrelated processes. The S6 design
  (one .xpc, both engine and child connect, broker holds the cached
  port) is fundamentally incompatible with this. This was NOT
  discoverable before instrumentation — the silent
  `Connection invalid` from `launchctl_print user/$UID/...` masked
  both the API misuse AND this design flaw.

- **Why deferred (per `feedback_defer_big_design_to_own_session`):**
  Fixing this needs a real architecture choice between four options,
  each non-trivial. Listing for the next session to pick from:
  1. **LaunchAgent install.** Bridge becomes a launchd-managed user-
     session-scope service via a `.plist` in `~/Library/LaunchAgents/`.
     Both engine and child connect via `xpc_connection_create_mach_service`
     (the ORIGINAL API!) in the `gui/$UID` domain. Pro: minimal code
     change. Con: requires one-time operator install step
     (`launchctl bootstrap`); .plist needs a stable binary path.
  2. **Engine acts as the XPC server.** Eliminates the bridge process
     entirely. Engine creates a Mach service listener (anonymous or
     well-known); child connects to it via name. Engine vends the
     serverPort directly. Pro: no separate broker process. Con:
     engine has to host an XPC listener — more code on the engine
     side; needs careful lifecycle to not interfere with the audio
     thread.
  3. **Anonymous endpoint handoff.** Engine creates the bridge
     connection in-process, gets an `xpc_endpoint_t`, serializes it,
     passes to the child via spawn-time env var or arg. Child
     reconstructs the endpoint with `xpc_connection_create_from_endpoint`.
     Pro: keeps the bridge process; no launchd .plist. Con: serializing
     XPC endpoints across `posix_spawn` is fiddly.
  4. **socketpair + SCM_RIGHTS port descriptor.** Skip XPC entirely.
     Engine and child share a UNIX socketpair from spawn time; engine
     sends the Mach send-right via Mach port descriptor in a
     `cmsghdr(SCM_RIGHTS)` ancillary message. Pro: no launchd
     dependency. Con: rewrites the bridge as a Mach-layer primitive,
     significant code change. Documented in
     `docs/superpowers/specs/2026-05-18-m7-s6-design.md` as the
     spec's fallback.
  Most-professional-and-elegant pick per the operator's standing
  preference is **Option 2 (engine as XPC server)** — eliminates the
  bridge process, no install step, single source of truth. Confirm
  before picking.

- **What's needed to finish:**
  1. Pick one of the four architecture options above (or a fifth I
     haven't enumerated). Write a brief design doc in
     `docs/superpowers/specs/2026-05-18-m7-s9-design.md`.
  2. Implement the chosen path.
  3. Verify via the existing os_log trace plus the manual eyes-on of
     the synthetic CLAP debug button.
  4. ctest stays green; add a new `[plugin-editor-xpc][in-app]`
     integration case if the chosen design has a testable seam.
  5. Update `continue.md` with the M7 S9 close.

- **What is NOT a follow-on:** the
  `docs/operator/macos-sandbox.md` Task 11 re-sign gap (S6 deviation
  #2) is now closed by Step 5 above. Leave that doc updated to
  reflect that the dev-loop Ninja generator now produces a
  Developer-ID-signed `.app` with sealed CodeResources.
