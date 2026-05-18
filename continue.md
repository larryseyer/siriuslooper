# Session Continuation — 2026-05-18 (M2 Session 2 shipped LOCALLY; M2 Session 3 next — Session 3 pushes both commits)

> **For a fresh chat picking this up cold:** read this whole file
> before doing anything. The user's `~/.claude/CLAUDE.md` and the
> project's auto-memory (`MEMORY.md` + `*.md` in the memory dir) are
> loaded automatically and contain the rules. This file is the
> *state*: what just shipped and what's queued next.

---

## RESUME HERE (2026-05-18 — M2 Session 2 committed locally; Session 3 next)

**M2 Session 2 of the V7 alignment plan is committed locally (NOT
pushed yet — Session 3 pushes).** Type shapes for the V3 mixer
architecture are in: `SignalType` (Audio/Midi/Video/File),
`signalTypeOf(InputKind)` helper, `TapeMode`
(CommitToTape/NonDestructive/NoTape), strong-typed `InputId` +
`ChannelId`, `Channel` aggregate, and `InputMixer` + `OutputMixer`
skeleton classes whose bodies all `assert(false && "M3-M5 stub")`.
The full `bash bash/autotest.sh` 4-phase gate passes — test count
**holds at 270** (no new tests this session, by plan). HEAD is
`ec1b5d9` on local `master`; `origin/master` is still at `ca92a08`.

### First moves for the M2 Session 3 chat

1. Read this file end-to-end.
2. Open `docs/superpowers/plans/2026-05-17-v7-alignment.md` lines
   240–252 — Session 3 is the one-liner: "Write skeleton tests for the
   new types; verify `bash autotest.sh` green; commit and push." Per
   plan line 238: instantiation + setter tests only; no buffer-flow
   assertions yet (those land in M3-M5).
3. Read the actual skeletons before writing tests — the M3 stubs make
   most setters assert-false at runtime, so the tests need to be
   shaped around what's safe to call:
   - `core/include/sirius/SignalType.h` — enum
   - `core/include/sirius/InputKind.h` — `signalTypeOf` (constexpr;
     fully testable, all 7 cases)
   - `engine/include/sirius/TapeMode.h` — enum
   - `engine/include/sirius/Channel.h` — `InputId`, `ChannelId` (both
     follow `TapeId`/`ConstituentId` house pattern: explicit constexpr
     ctor, `value()`, `==`/`!=`); `Channel` aggregate with `id`,
     `signalType`, `source`, `tapeMode` fields
   - `engine/include/sirius/InputMixer.h`, `OutputMixer.h` — methods
     declared, .cpp bodies `assert(false && "M3-M5 stub")`. Tests can
     verify *construction* (default ctor + dtor are real, non-asserting)
     but should NOT call any setter / `addChannel` / `processBuffer` etc.
     because every body asserts. **Per Risks note line 257 this is
     intentional — don't "fix" it by making bodies no-op.**
4. Write the three new test files (Catch2 SECTIONs; match the style of
   `tests/LatencyTimingTests.cpp` / `tests/InputDescriptorTests.cpp`):
   - `tests/SignalTypeTests.cpp` — `signalTypeOf(InputKind)` exhaustive
     mapping; Catch2 tag `[signal-type]`. Compile-time `static_assert`s
     are fine since `signalTypeOf` is `constexpr`.
   - `tests/ChannelTests.cpp` — `InputId`/`ChannelId` strong-typing
     (constexpr ctor, value, ==/!=); `Channel` aggregate construction
     and field access; tag `[channel]`.
   - `tests/InputMixerTests.cpp` + `tests/OutputMixerTests.cpp` — *only*
     "default ctor + dtor don't crash" and "type is non-copyable/movable
     per current design" — nothing that calls a stubbed method. Tag
     `[input-mixer]` / `[output-mixer]`. **Plan deviation possible:**
     if `InputMixer`/`OutputMixer` have nothing test-worthy this thin,
     consider deferring these two test files to M3 when bodies are real,
     and just shipping `SignalTypeTests.cpp` + `ChannelTests.cpp` for
     Session 3. Plan line 228 enumerates all three so do them unless
     the assert-false floor makes the test file vacuous.
5. Wire `tests/CMakeLists.txt` — find the existing
   `target_sources(SiriusTests PRIVATE …)` block (or equivalent) and
   add the new files.
6. Verify with `rm -rf build build-xcode && bash bash/autotest.sh`
   (note: rm sometimes needs a retry on `build-xcode/` — it's a known
   transient on this filesystem. If `rm -rf build-xcode` exits non-zero
   with "Directory not empty," just retry once).
7. Commit as `feat: M2 Session 3 — skeleton tests for SignalType / Channel / Mixer types`,
   then `git push origin master`. This push will carry BOTH `ec1b5d9`
   (Session 2) and the Session 3 commit to origin in one push — that's
   intentional, per the original plan break-out (Session 3 is the push
   step for the M2 milestone).
8. After push, update continue.md → "M2 complete, M3 next."

### M2 Session 2 — what landed locally (commit `ec1b5d9`)

10 files changed, 327 insertions, 1 deletion. Two modifications + eight
new files:

- `core/include/sirius/SignalType.h` (new — enum `{ Audio, Midi, Video, File }`)
- `core/include/sirius/InputKind.h` (modified — added `constexpr SignalType signalTypeOf(InputKind) noexcept`; the seven `InputKind` cases collapse to four `SignalType` cases — Audio/Midi/Video pass through; Control/ParameterAutomation/Transport/System all → File, per plan Risks note line 256)
- `engine/include/sirius/TapeMode.h` (new — enum `{ CommitToTape, NonDestructive, NoTape }`)
- `engine/include/sirius/Channel.h` (new — `InputId`, `ChannelId` strong-typed wrappers around `std::int64_t` matching the house `TapeId`/`ConstituentId` style; `Channel` struct with `id`, `signalType`, `source`, `tapeMode`)
- `engine/src/Channel.cpp` (new — placeholder TU; comment-only today, exists so M3 can land non-trivial ctors without re-touching CMake)
- `engine/include/sirius/InputMixer.h` (new — V3 §2.1 sketch: `registerInput`, `setInputRawDirect`, `setInputEnabled`, `addChannel`, `removeChannel`, `setChannelTapeMode`, `processBuffer`. Future-only params like `ChannelConfig`, `BusId`, `InputBuffers`, `OutputDestinations`, `ChannelDefaults`, `DirectRouting` are held by `/* M3: ... */` comments inside the signatures — kept out of the type system until M3 designs them.)
- `engine/src/InputMixer.cpp` (new — every body `assert(false && "...stub")`; default ctor/dtor real)
- `engine/include/sirius/OutputMixer.h` (new — V3 §2.2 sketch: `addChannel`, `routeChannelToOutput`, `renderBuffer`; same `/* M3: */` placeholder convention)
- `engine/src/OutputMixer.cpp` (new — same assert-false shape)
- `engine/CMakeLists.txt` (modified — appended `src/Channel.cpp`, `src/InputMixer.cpp`, `src/OutputMixer.cpp` to `SiriusEngine`)

### Plan deviations & subtle constraints to carry forward

1. **Method names are PascalCase, not snake_case.** V3 transition guide
   lines 117–139 / 164–183 wrote the sketch in `snake_case`
   (`register_input`, `add_channel`, `process_buffer`). The actual
   skeleton uses JUCE/Sirius house style: `registerInput`,
   `addChannel`, `processBuffer`. Session 3 tests must use the
   PascalCase names. If a future session wants to flip the project to
   snake_case it's a global decision, not an M2-local one.
2. **No `ChannelConfig`/`BusId`/`InputBuffers`/`OutputDestinations`
   types exist yet.** Their argument positions are placeholder C-style
   comments inside the method signatures. Session 3 tests must not
   reference these names. M3 is the milestone that designs them.
3. **`Channel` is a plain aggregate, not a class with members.** It
   has four fields right now (`id`, `signalType`, `source`, `tapeMode`).
   Test it with brace-init; M3 will likely promote it to a class with
   invariants when `ProcessingChain` and `destinations` land.
4. **No `Control` SignalType.** Locked decision: `InputKind::Control`
   maps to `SignalType::File`. Don't add a fifth enumerator in
   Session 3 — push back if the test feels like it wants one.
5. **`InputId` and `ChannelId` live in `engine/include/sirius/Channel.h`
   for now,** not in `core/`. The header comment notes "promote to
   core/ in M3+ if it spreads into persistence or UI layers." Session 3
   tests live in `tests/` which already pulls Engine headers, so this
   is fine.
6. **Channel.cpp has no symbols.** It's a `.cpp` with one comment.
   This compiles to an empty .o file and links cleanly. Don't be
   alarmed by "wait, does this file do anything?" — it's intentional
   scaffolding (plan line 222–223 calls for the .cpp; the empty TU is
   the smallest correct form).
7. **`rm -rf build-xcode` may need a retry.** Saw a "Directory not
   empty" exit code 1 once this session; immediate retry succeeded.
   Filesystem race, not a code issue. Not worth investigating unless
   it becomes chronic.

### Where M2 acceptance criteria stand (after Session 2)

- [x] Membrane → LatencyTiming rename + namespace flip (M2 Session 1)
- [x] `core/include/sirius/SignalType.h` added
- [x] `signalTypeOf(InputKind)` helper added to InputKind.h
- [x] `engine/include/sirius/InputMixer.h` + `OutputMixer.h` skeletons added (assert-false bodies)
- [x] `engine/include/sirius/Channel.h` added with strong-typed `ChannelId` + `InputId`
- [x] `engine/include/sirius/TapeMode.h` added
- [x] Existing tests stay green (270/270; rename is mechanical, skeletons aren't called)
- [ ] Skeleton tests for new types (Session 3)
- [ ] Push to `origin/master` (Session 3; carries Session 2's `ec1b5d9` + Session 3's commit)

What landed this session (M2 Session 1):

- `engine/include/sirius/LatencyTiming.h`, `engine/src/LatencyTiming.cpp`
  — new, namespace `sirius::latency`. Content is the verbatim former
  Membrane content with namespace + error-message string updates only.
  Two free functions: `inboundCaptureTime`, `outboundPresentTime`.
- `engine/include/sirius/Membrane.h`, `engine/src/Membrane.cpp` —
  deleted. Zero remaining `sirius::membrane::` references in the tree
  (verified by grep).
- `tests/LatencyTimingTests.cpp` — renamed from `MembraneTests.cpp`.
  Catch2 tags updated `[membrane]` → `[latency]`. Test descriptions /
  prose updated. Assertions byte-identical.
- `engine/CMakeLists.txt`, `tests/CMakeLists.txt` — updated.
- `tests/AudioCallbackTests.cpp` — added a `pumpUntilPositive` helper
  so the M1 Session 3 load-publish tests don't depend on a single
  cold-cache callback registering a non-zero tick delta. 1000-iteration
  safety cap so a fundamentally-broken clock can't hang the test.

**Plan deviations from M2 Session 1:**

- The plan predicted "expected hits in `engine/src/RenderPipeline.cpp`,
  `app/MainComponent.cpp`" for `sirius::membrane::` usages. **Zero
  actual hits in either** — `Membrane.{h,cpp}` was already unused by
  product code, only referenced by its own test. The rename surface
  was 3 files, not the 5+ the plan implied. Session 2's "Files
  touched" enumeration is more accurate (no widespread call-site
  rewrites needed).
- `video/include/sirius/FrameMembrane.h` + `video/src/FrameMembrane.cpp`
  are a separate concept (video frame timing) and are **explicitly
  out of scope** for M2 per the plan's narrow wording. Left untouched.

What landed this session (M1 Session 3):

- `audio/include/sirius/AudioCallback.h`, `audio/src/AudioCallback.cpp`
  — added `setAsrcInputs(std::vector<Asrc*>)`, `setAsrcOutputs(...)`,
  `setCalibration(const AudioDeviceCalibration*)` — non-owning,
  message-thread, set-once. New `std::atomic<double> lastCallbackElapsedSec_`
  published via `lastCallbackElapsedSec()`. The callback now wraps its
  work with `juce::Time::getHighResolutionTicks` and stores elapsed
  seconds at the end of every buffer. Buffer body is otherwise
  unchanged — still `memcpy + lmc_->advanceBySamples`.
- `app/MainComponent.{h,cpp}` — owns 2 input ASRCs + 2 output ASRCs
  (`std::unique_ptr<Asrc>`, `maxIoRatio=1.01`, quality from
  `EngineConfig`), one `AudioDeviceCalibration::identity()`, one
  `OverloadProtection`, one `RetroactiveRing<std::uint8_t>{1024}` (the
  `std::uint8_t` is provisional until the real tape-event type lands
  in M3/M4). The 30 Hz `timerCallback` reads
  `audioCallback_->lastCallbackElapsedSec()`, divides by
  `bufSize / rate`, calls `overloadProtection_.reportLoad(fraction)`.
  Diagnostics row in the Preparation pane gained a new `Load: X%
  of budget (shed: N)` line; the label height bumped 60 → 84 px to fit.
- `docs/RT_SAFETY_CONTRACT.md` — four `TBD` rows filled, with notes
  per piece (Asrc held-not-invoked; OverloadProtection driven from
  the message thread via atomic-published elapsed; RetroactiveRing
  explicitly off the audio thread; AudioDeviceCalibration held at
  identity until M8). New post-table paragraph describes the three
  shapes M1 Session 3 introduced.
- `tests/AudioCallbackTests.cpp` — 3 new `[load-publish]` cases:
  elapsed is 0 before any callback; positive (< 1 ms) after one buffer;
  resets to 0 on `audioDeviceStopped()`.

**Operator decisions locked in 2026-05-17 brainstorm (captured in
`~/.claude/plans/read-continue-and-proceed-partitioned-diffie.md`):**

- Q1 — *Strict scaffolding* (option A): engine pieces are reachable
  from the audio path's owners but the callback body remains
  `memcpy + lmc_->advanceBySamples`. ASRC at unity is NOT bit-identical
  (cubic/sinc interpolation), so routing through it would have broken
  the existing identity tests for no M1 benefit.
- Q2 — *Atomic publish, message-thread consumes* (option A). Mid-session
  refinement: the atomic publishes **elapsed seconds**, not the load
  fraction — division moves to the message thread. Net result:
  smaller audio-thread footprint (one `mach_absolute_time` pair + one
  atomic store; no division), testable without a JUCE device mock,
  same operator-facing behaviour.

**Subtle constraints worth carrying forward:**

- The diagnostics pane height grew (60 → 84 px) for the new Load
  line. If M2 adds another diagnostics line it'll likely need
  another bump, or a switch to dynamic height.
- `Asrc` is held by `AudioCallback` but not invoked. Compiler will
  warn about unused if any future cleanup tightens warnings — the
  member is intentional scaffolding; M2-M5 grow the call sites.
- `RetroactiveRing<std::uint8_t>` is a placeholder type parameter.
  The real `T` is the tape-event type that lands with the M3 SPSC
  tape-event queue. Renaming the member at that point is acceptable.

### Where M1 acceptance criteria stand (all green)

- [x] App registers an audio device on startup at user default sample
      rate / buffer size. (M1 Session 1 + 2/2 channel default.)
- [x] `AudioCallback::audioDeviceIOCallbackWithContext` runs identity
      pass-through within buffer budget. (Verified by autotest.)
- [x] LMC sample-clock fed from device callback. (M1 Session 2,
      tests `[sample-clock]`.)
- [x] `Asrc` / `OverloadProtection` / `AudioDeviceCalibration` /
      `RetroactiveRing` wired in. (M1 Session 3 — see RT_SAFETY_CONTRACT.md.)
- [x] `docs/RT_SAFETY_CONTRACT.md` lives in the repo with the per-PR
      audit checklist. (M1 Session 2 + Session 3 audit rows.)
- [x] Existing 256 ctest cases stay green — 270/270 now.

M2 (Membrane → Mixer rename + SignalType + Channel) is unblocked.

The auto-testing milestone (sections 0-5 below) is **closed and
shipped**. The CI signing handoff (3 of 6 secrets pending) is **still
open** — operator-only Keychain/AppleID work, doesn't gate M1+.
Treat sections 0-5 as historical state.

---

## HISTORICAL — Auto-testing CI signing handoff (still operator-pending, not blocking V7 work)

Auto-testing milestone is shipped on master. CI workflow is in place
but **first run will fail** until the remaining three repo secrets are
added.

**Already done this session (live in repo settings):**

| Secret | Status | Value |
|---|---|---|
| `APPLE_TEAM_ID` | ✓ set | `RR5DY39W4Q` |
| `APPLE_ID` | ✓ set | `itunes@larryseyer.com` |
| `KEYCHAIN_PASSWORD` | ✓ set | random 32-char base64 (throwaway, never leaves CI) |

**Still needed (require operator hands — Keychain GUI + appleid.apple.com):**

1. **`DEVELOPER_ID_CERT_P12_BASE64`** + **`DEVELOPER_ID_CERT_PASSWORD`** —
   export the Developer ID Application cert from Keychain Access:
   - Keychain Access → "login" → My Certificates
   - Find `Developer ID Application: Larry Seyer (RR5DY39W4Q)`. The
     disclosure triangle MUST show a private key under it (else the
     export is useless for signing)
   - Right-click → Export → save as `~/Desktop/sirius-devid.p12`,
     format: Personal Information Exchange (`.p12`)
   - Keychain prompts for an export password — pick anything
     memorable, this becomes `DEVELOPER_ID_CERT_PASSWORD`
   - Keychain may also prompt for your login password to release the
     private key (normal)

2. **`APPLE_APP_PASSWORD`** — app-specific password for notarytool:
   - https://appleid.apple.com → sign in as `itunes@larryseyer.com`
   - Sign-In and Security → App-Specific Passwords → "+"
   - Label `sirius-notarytool` and generate
   - Copy the 19-char password (`xxxx-xxxx-xxxx-xxxx`)

**Two paths to finish the handoff** (operator's choice on return):

- **Path A (operator hands the values to Claude):** drop the .p12 at
  `~/Desktop/sirius-devid.p12`, tell Claude the export password and the
  app-specific password. Claude runs the three `gh secret set` lines.
- **Path B (operator runs the gh commands):**
  ```
  gh secret set DEVELOPER_ID_CERT_P12_BASE64 < <(base64 -i ~/Desktop/sirius-devid.p12)
  gh secret set DEVELOPER_ID_CERT_PASSWORD --body "<the .p12 password>"
  gh secret set APPLE_APP_PASSWORD --body "xxxx-xxxx-xxxx-xxxx"
  ```

**After all six secrets are in place — verification:**

```
gh secret list                    # confirm 6 entries
gh workflow run ci-macos-signed.yml
gh run watch                      # follow the live run
```

Expected: build + sign + verify all green. The "GUI smoke (best-effort)"
step may pass or skip on the GitHub-hosted runner — either is acceptable
(see continue.md §4 for why).

Once the workflow runs green: delete `~/Desktop/sirius-devid.p12`
(security hygiene — the secret is now in GitHub; the local copy is
no longer needed and exposes the private key if leaked). Then the
auto-testing milestone is fully closed out and the next session can
start on the white-paper alignment pass (§6).

---

## 0. Headline

**Auto-testing infrastructure milestone shipped end-to-end** on master
this session. Three commits, all pushed:

| SHA       | Subject                                                                                      |
|-----------|----------------------------------------------------------------------------------------------|
| `4e8a1df` | fix: smoke-persistence.sh — direct binary launch + recursive AX walk + dialog-targeted clicks |
| `f68aa3c` | feat: bash/autotest.sh — local 4-phase macOS verification driver                              |
| (next)    | feat: ci-macos-signed.yml — signed-build + smoke CI workflow + this doc update                |

**Phase A — `bash/smoke-persistence.sh` patched and verified.** Five
AppleScript / Launch Services landmines worked around (catalogued in
§2). Round-trips Save → Load against the signed bundle, exits 0 with
"refs in file: 2" proving v2 shared-encoding ran.

**Phase B — `bash/autotest.sh` runs four phases locally** in ~25s
total on an Apple Silicon dev machine: headless ctest → signed Xcode
bundle build → codesign + spctl verification → GUI smoke. All four
green on master.

**Phase C — `.github/workflows/ci-macos-signed.yml` added** as a
separate workflow from `ci.yml`. Will fire on the next push to master
(this commit). Operator one-time setup: six repo secrets must be
added before the workflow can succeed — see §5.

**Next session = white-paper alignment pass** per the standing
operator sequencing (signing → auto-testing → white-paper alignment).
Auto-testing landed; alignment is up. See §6 for the picked-up
context for that work.

---

## 1. Build + test state

```bash
cd /Users/larryseyer/SiriusLooper
bash bash/autotest.sh        # full 4-phase verification, ~25s
```

Or individual gates:

```bash
# Headless only
cmake --build build --target SiriusTests
ctest --test-dir build --output-on-failure        # 256 / 4283 expected

# Signed bundle only
cmake --build build-xcode --config Release --target SiriusLooper

# GUI smoke against the signed bundle
APP_BUNDLE="build-xcode/app/SiriusLooper_artefacts/Release/Sirius Looper.app" \
  bash bash/smoke-persistence.sh
```

The signed `.app` lives at `build-xcode/app/SiriusLooper_artefacts/Release/Sirius Looper.app`.
Developer ID Application: Larry Seyer (RR5DY39W4Q), hardened runtime,
audio-input entitlement, spctl-accepted.

The dev-loop `build/.app` is unchanged — ad-hoc-signed, fast iteration.

---

## 2. AppleScript / macOS landmines documented this session (for future GUI smoke work)

Each of these bit hard during Phase A. Recorded here so the next time
someone touches `bash/smoke-persistence.sh` or writes a new
osascript-based GUI driver, they don't have to rediscover them.

1. **`open ...app` returns -10825 on dev-tree paths.** Launch Services
   refuses to launch the Developer-ID-signed bundle from
   `build-xcode/...` even though `spctl` accepts it. The ad-hoc
   sibling at `build/...` opens fine. Suspected cause: the ad-hoc
   bundle's invalid `Identifier=Sirius Looper` (string with space)
   collides in LS with the signed bundle's valid
   `com.larryseyer.siriuslooper`. Workaround: launch the binary
   directly via `${APP_BUNDLE}/Contents/MacOS/${APP_NAME}` —
   bypasses LS entirely, more reliable for automation anyway.
   Root cause filed as its own todo entry.

2. **`entire contents of window 1` strips role info.** Iterating
   returns elements where `role of elt` doesn't resolve cleanly; the
   `AXButton` role check matched zero elements even when buttons were
   present. Workaround: explicit two-level recursion using
   `every UI element of window 1` then `every button of grp`.

3. **Tab buttons are nested one AXGroup deep.** JUCE's
   `TabbedComponent` exposes tab buttons as AXButton children of an
   AXGroup, not at the window's top level. Save/Load on the
   Preparation pane are similarly nested. Recursion in #2 catches
   both.

4. **`keystroke "g" using {command down, shift down}` silently no-ops
   inside NSSavePanel/NSOpenPanel on Sequoia** when the filename field
   has focus. The text field appears to eat the modified keystroke.
   Workaround: `key code 5 using {command down, shift down}` (5 =
   physical 'g' keycode) fires the system shortcut reliably.

5. **NSSavePanel/NSOpenPanel open as separate top-level windows,
   not sheets.** `sheets of window 1` returns 0 after clicking
   Save/Load. The dialog is a sibling window whose name starts with
   the title prefix ("Save Sirius session..." / "Load Sirius
   session..."). Target it by `first window whose name starts with
   "..."` and click the action button by name. Bonus: NSOpenPanel's
   "Open" button stays *disabled* until a file is selected in the
   listing — Cmd+Shift+G navigates to the directory but doesn't
   select, so the smoke script falls back to Return when the action
   button is disabled.

6. **Activate-and-keystroke must be in the same osascript block.**
   When `tell application "X" to activate` and `keystroke ...` are
   issued from separate osascript invocations, focus can slip back
   to the terminal between calls and the keystrokes land in the
   wrong app. Solution: combine them in one heredoc.

---

## 3. The three small follow-ups surfaced this session

All filed in `todo.md` near the top. None block anything; all are
"while we're in this neighborhood" cleanups.

1. **`com.apple.security.get-task-allow=true` in signed entitlements.**
   Visible in `codesign -dv --entitlements -` output. Fine for non-
   notarized dev builds; rejected by Apple's notarization service.
   Must be `false` (or absent) before any notarized release. Likely a
   Xcode default the CMake block needs to override.

2. **`open` returns -10825 on the signed dev-tree bundle.** Root cause
   of landmine #1 above. Workaround in place in the smoke script;
   investigation deferred. End-user launches against a notarized
   bundle in /Applications are not affected.

3. **Ad-hoc `build/` bundle has invalid `Identifier=Sirius Looper`.**
   Should be `com.larryseyer.siriuslooper` to match the Xcode-gen
   build. The mismatch is the leading suspect for #2's LS confusion.
   Fix the CMake config that sets the bundle ID on the Unix Makefiles
   target.

---

## 4. CI status

`ci.yml` (the original push/PR matrix):
- macOS: green (headless tests).
- Linux + Windows: red on a pre-existing int64→juce::var ambiguity in
  `persistence/src/SessionFormat.cpp`. **Explicitly deferred** under
  the `feedback-mac-first-linux-windows-last` rule — not a candidate
  for "what's next."

`ci-macos-signed.yml` (added this session):
- Will fire on the next push to master. **First run will fail**
  because the six required secrets are not yet in the repo. Operator
  step: add them per §5, then push (or `workflow_dispatch`) to verify.
- GUI smoke step is `continue-on-error: true` from day one — GitHub-
  hosted macOS runners can't grant the runner user Accessibility
  permission without SIP-disabling tricks that hosted runners don't
  support. Build + sign + codesign-verify is the hard gate; smoke is
  best-effort signal.

---

## 5. Operator one-time setup before CI signing works

In GitHub repo Settings → Secrets and variables → Actions, add:

| Secret name                    | Value source                                                                                                |
|--------------------------------|-------------------------------------------------------------------------------------------------------------|
| `DEVELOPER_ID_CERT_P12_BASE64` | Export `Developer ID Application: Larry Seyer (RR5DY39W4Q)` from Keychain Access as `.p12` with a password, then `base64 -i Developer-ID.p12 \| pbcopy` |
| `DEVELOPER_ID_CERT_PASSWORD`   | The `.p12` export password just set                                                                         |
| `APPLE_ID`                     | `itunes@larryseyer.com` (per `reference-apple-id` memory)                                                   |
| `APPLE_APP_PASSWORD`           | App-specific password from appleid.apple.com (NOT the Apple ID password — generate one specifically for notarytool) |
| `APPLE_TEAM_ID`                | `RR5DY39W4Q`                                                                                                |
| `KEYCHAIN_PASSWORD`            | Any opaque string the operator picks — used as the throwaway CI keychain password                            |

`APPLE_ID` / `APPLE_APP_PASSWORD` / `APPLE_TEAM_ID` aren't strictly
needed for the current workflow (no notarization step yet), but
they're listed here so they get added once and are ready when
notarization moves into a release-tag workflow.

After secrets land: trigger the workflow via `workflow_dispatch` in
the Actions tab (or just push), and verify the run goes green
("GUI smoke: PASS" or "GUI smoke: SKIPPED/FAILED on CI runner" —
either is acceptable; build + sign + verify must pass).

---

## 6. Next milestone — white-paper alignment pass — SUPERSEDED-AND-SPEC'D 2026-05-17

The alignment pass that this section queued has been spec'd in full at
`docs/superpowers/plans/2026-05-17-v7-alignment.md` (24 milestones across
11 parts; M1 = Audio I/O foundation + RT-safety contract audit; see
RESUME HERE at the top of this file). The original V2 white paper
referenced below is superseded by V7 (`docs/Sirius_Looper.md`); the
V2→V7 transition guide (`docs/sirius-looper-v2-to-v7-transition.md`)
is the bridge.

Original §6 contents preserved for historical context follow; treat as
read-only:

> Per the operator's stated sequence (signing → auto-testing → white-
> paper alignment). Auto-testing is done; alignment is up.
>
> **Reading order before starting alignment work:**
>
> 1. `docs/Sirius Looper Whitepaper V2.md` — the white paper.
>    *(Superseded — read V7 at `docs/Sirius_Looper.md` instead.)*
> 2. `docs/Sirius Looper User Guide.md` — the operator-facing how-to.
> 3. `docs/superpowers/specs/2026-05-16-shared-placement-design.md` — the
>    most-recent shipped spec.
> 4. `todo.md` — alignment-adjacent entries.
>
> **What this session does NOT decide:** the scope, structure, or
> deliverables of the alignment pass. That's the next session's first-
> half work (brainstorm + spec) before any code lands.
>    *(That brainstorm + spec is what 2026-05-17 produced. The plan
>    file is the output.)*

---

## 7. Authoritative references

- `~/.claude/CLAUDE.md` — global rules (auto-loaded).
- `~/.claude/projects/-Users-larryseyer-Sirius-Looper/memory/MEMORY.md`
  — auto-memory index (auto-loaded). Two new entries this session:
  - `feedback-mac-first-linux-windows-last` (Platform order:
    **macOS → iOS → Windows → Linux**; iOS = AUv3 only)
  - `feedback-esc-while-typing-is-not-abort` (silent tool rejections
    usually mean the operator is mid-message)
- `todo.md` — deferred items register. Auto-testing milestone marked
  SUPERSEDED-AND-IMPLEMENTED at the top; three new follow-ups filed
  beneath it; legacy GUI-smoke and Load-dialog entries updated to
  reflect resolution.
- `/Users/larryseyer/.claude/plans/read-continue-and-proceed-floating-pelican.md`
  — the auto-testing plan that just shipped. Self-contained, can be
  archived or kept as a structural template.
- `bash/autotest.sh` — single command for full local verification.
- `bash/smoke-persistence.sh` — GUI smoke driver (operator must have
  Accessibility access granted to the shell that runs it).
- `.github/workflows/ci-macos-signed.yml` — signed CI workflow (six
  secrets required, see §5).
- `docs/Sirius Looper Whitepaper V2.md` and
  `docs/Sirius Looper User Guide.md` — next milestone's primary
  source material.

---

## 8. One-paragraph orientation if everything else has changed

Auto-testing infrastructure is in place. `bash/autotest.sh` is the
local one-shot gate; `ci-macos-signed.yml` is the per-push CI gate
(needs operator-added secrets to actually run, see §5). Next
milestone is the white-paper alignment pass — that's a design
session, not a code session, so start with brainstorm + spec before
touching `app/` or `ui/`. Linux/Windows remain explicitly deferred.
