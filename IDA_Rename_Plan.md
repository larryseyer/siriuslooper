# Plan: Rename "Sirius Looper" → "IDA"

## Context

The product formerly named *Sirius Looper* (and briefly considered for renaming to *Serious Looper*) is being renamed to **IDA — Idea Development Arranger**, the looping environment counterpart to **OTTO — Organic Timing & Trigger Orchestrator**. Both products live under **AutomagicArt**. See `IDA_Naming_Decision.md` for the full rationale.

New canonical home: **`automagicart.com/ida`**. No new vanity domain purchased; the parent brand carries discovery. Both `siriuslooper.com` and `ottodrums.com` will be allowed to lapse (auto-renew turned off) — no SEO equity to preserve since neither product has launched.

Operator decisions (confirmed):

- **Full rename** across all source, build, docs, website, and OTTO cross-project inbox.
- **GitHub repo rename**: `siriuslooper` → `ida`.
- **Local dir rename**: `/Users/larryseyer/SiriusLooper` → `/Users/larryseyer/IDA`.
- **Archives + session docs renamed** for uniformity (V1–V7 whitepapers, continue.md, todo.md, progress.txt).
- **Both old domains lapse**: `siriuslooper.com` and `ottodrums.com` (pre-launch, no equity).
- **larryseyer.com is a separate repo** — handled in its own session; this plan calls it out but does not inventory it.
- **OTTO whitepaper updates** are part of this plan (Phase 5), not deferred.

A separate ralph loop is running on `master` overnight in another terminal. **No work in this repo until ralph halts.** Doing this rename mid-loop will cause merge hell and a likely HALT cascade.

---

## Casing conventions (lock these in before mechanical pass)

This rename is bigger than Sirius→Serious because we're replacing a multi-syllable word with a three-letter acronym. Different code contexts need different casings. The mechanical pass below uses these consistently:

| Context | Old | New | Rationale |
|---|---|---|---|
| Brand display, UI strings, binary name, App display | `Sirius Looper` | `IDA` | All-caps acronym matches OTTO branding |
| Brand with tagline (where appropriate) | `Sirius Looper` | `IDA — Idea Development Arranger` | Long form for About box, marketing |
| Document filenames | `Sirius_Looper_Whitepaper_V7.md` | `IDA_Whitepaper_V8.md` | All-caps, parallels OTTO_WhitePaper.md |
| C++ namespace | `sirius::` | `ida::` | Lowercase, std convention |
| C++ class/struct prefix | `SiriusPalette` | `IdaPalette` | Acronym-as-word (JUCE/Google style); `IDAPalette` reads ugly |
| Macros / preprocessor | `SIRIUS_` | `IDA_` | All-caps macro convention |
| CMake target (binary name) | `SiriusLooper` | `IDA` | Matches brand; produces `IDA.app` |
| CMake alias namespace | `Sirius::` | `Ida::` | Pascal-treated acronym; consistent with class style |
| Directory names (source tree) | `ui/include/sirius/` | `ui/include/ida/` | Lowercase, matches namespace |
| Bundle ID | `com.larryseyer.siriuslooper` | `com.larryseyer.ida` | Lowercase reverse-DNS standard |
| Domain (in code/configs) | `siriuslooper.com` | `automagicart.com/ida` | New canonical path |
| Git trailer | `Sirius-Origin:` | `Ida-Origin:` | Title-Case-Hyphenated trailer convention |
| File extension (project files) | `.sirius.json` | `.ida.json` | Lowercase, matches namespace |
| Local working dir | `/Users/larryseyer/SiriusLooper` | `/Users/larryseyer/IDA` | All-caps matches brand visibility in shell |
| GitHub repo | `siriuslooper` | `ida` | Lowercase, matches product name |
| Backup folder | `Sirius Looper Backup` | `IDA Backup` | Matches brand |

The mechanical pass must apply these in the correct order (longest-match-first) to avoid double-substitutions. The script in §1a does this.

---

## Pre-flight (operator)

Before any rename work begins:

1. **Stop ralph.** Either let it run to natural completion or `<promise>HALTED</promise>` it manually. Confirm `git status` is clean on `master`. Push any pending ralph commits so the remote is the source of truth.

2. **Tag the last pre-rename build**: `git tag pre-ida-rename` on master so we can always diff back. Push the tag.

3. **Back up** via `bash/bu.sh` so the Dropbox snapshot captures the pre-rename state. (The backup destination string still references "Sirius Looper Backup" at this point — that's fine; this snapshot becomes the rollback point.)

4. **Turn off auto-renew on both old domains** at their respective registrars:
   - `siriuslooper.com` — let it lapse. No redirect needed; product never launched, no equity to preserve.
   - `ottodrums.com` — let it lapse. Same reasoning.
   - This is a one-click setting at most registrars; no DNS changes needed today.

5. **Confirm `automagicart.com` DNS is healthy** and that you have access to add a `/ida` path or subdirectory under it. Phase 3 builds on this.

---

## Phase 1 — Repo-internal rename (one branch, one giant commit + cleanup)

Work on a temporary branch `rename/ida` cut from clean `master`. Single big mechanical commit, then small follow-ups for things that can't be sed'd. Merge fast-forward back to `master`. No PR — solo dev workflow.

### 1a. Mechanical text replacement (one commit)

Run a scripted pass with these substitutions, in this order (longest match first to avoid double-substitutions). Order matters because some replacements would otherwise be re-substituted by later patterns.

| # | Find | Replace |
|---|---|---|
| 1 | `Sirius Looper Backup` | `IDA Backup` |
| 2 | `Sirius Looper — Idea` (or any tagline form) | `IDA — Idea Development Arranger` |
| 3 | `Sirius Looper` | `IDA` |
| 4 | `siriuslooper.com` | `automagicart.com/ida` |
| 5 | `SiriusLookAndFeel` | `IdaLookAndFeel` |
| 6 | `SiriusBinaryData` | `IdaBinaryData` |
| 7 | `SiriusPalette` | `IdaPalette` |
| 8 | `SiriusEngine` | `IdaEngine` |
| 9 | `SiriusCore` | `IdaCore` |
| 10 | `SiriusVideo` | `IdaVideo` |
| 11 | `SiriusNet` | `IdaNet` |
| 12 | `SiriusTests` | `IdaTests` |
| 13 | `SiriusUi` | `IdaUi` |
| 14 | `SiriusLooper` (CMake target, no space) | `IDA` |
| 15 | `siriuslooper` (lowercase identifier) | `ida` |
| 16 | `SIRIUS_` | `IDA_` |
| 17 | `Sirius::` (CMake alias) | `Ida::` |
| 18 | `sirius::` (C++ namespace) | `ida::` |
| 19 | `sirius_plugin_host` | `ida_plugin_host` |
| 20 | `sirius-smoke` | `ida-smoke` |
| 21 | `.sirius.json` | `.ida.json` |
| 22 | `Sirius-Origin:` (commit trailer) | `Ida-Origin:` |
| 23 | `Sirius/` (path segments) | `Ida/` |
| 24 | `/sirius/` (include path) | `/ida/` |
| 25 | `"Sirius"` (quoted product name) | `"IDA"` |
| 26 | ` Sirius ` (whitespace-bounded prose) | ` IDA ` |

**Critical:** Rows 2 and 3 must run *before* row 14 because "SiriusLooper" appears inside phrases like "Sirius Looper" in some docs without spaces. The script must process pattern-by-pattern, not file-by-file, to keep ordering consistent across the codebase.

**Exclude paths**: `build/`, `external/OTTO/` (handled in Phase 5), `.git/`, `docs/archive/` content (handled in Phase 1c), `build-xcode/` (should already be gone), any OTTO assets dir, anything matching `*.lock` or vendor directories.

**Tool**: a bash script using `rg --files-with-matches <pattern>` piped to `sed -i ''` (BSD sed syntax on macOS). Verify the script in dry-run mode first by diffing one substitution against `git grep`. Run row-by-row with a `git diff --stat` between rows so any unexpected fanout shows up immediately.

Commit:

```
refactor: rename Sirius Looper → IDA (mechanical pass)

Product rename per IDA_Naming_Decision.md. Canonical home is now
automagicart.com/ida; bundle ID com.larryseyer.ida; namespace ida::;
binary IDA.app.
```

### 1b. File and directory renames

After the text pass, rename files (`git mv`) so paths match identifiers:

- `ui/include/sirius/` → `ui/include/ida/`
- `ui/include/sirius/SiriusPalette.h` → `ui/include/ida/IdaPalette.h` (and any other `Sirius*.h` siblings)
- `app/SiriusLooper.macos.entitlements` → `app/IDA.macos.entitlements`
- `host_process/sirius_plugin_host.entitlements` → `host_process/ida_plugin_host.entitlements`
- `docs/Sirius_Looper_Whitepaper_V7.md` → `docs/IDA_Whitepaper_V8.md` (note version bump — see §1c-bis below)
- `docs/Sirius_Looper_User_Guide.md` → `docs/IDA_User_Guide.md`
- `docs/design/sirius-internal-fx.md` → `docs/design/ida-internal-fx.md`
- `docs/design/sirius-colour-method.md` → `docs/design/ida-colour-method.md`

Build artifact paths (`build/app/SiriusLooper_artefacts/...`) regenerate automatically once CMake reconfigures with the new target name. The new artifact path will be `build/app/IDA_artefacts/...`.

### 1c. Archive renames (docs/archive/)

Rename the archive files (V1–V6 whitepapers + transition doc) to use "IDA" in filenames. Leave their **internal content** unchanged — those are frozen historical record of when the product was named Sirius Looper, and rewriting their prose would be revisionist. Add a one-paragraph preamble to each archive file:

> Historical archive. This document was written when the product was named "Sirius Looper". The product was renamed to "IDA — Idea Development Arranger" on YYYY-MM-DD as part of the AutomagicArt brand structure (IDA is the looping environment counterpart to OTTO). References to "Sirius Looper" below reflect the contemporaneous name and are preserved as-is. See `IDA_Naming_Decision.md` for the full rationale.

Single commit: `docs: rename archives to IDA, preserve historical content`.

### 1c-bis. Whitepaper V8 (new architectural version)

This rename is substantive enough to warrant a whitepaper version bump. The V7 → V8 transition isn't just a find-replace; it's the moment the architectural document inherits the new product identity. Recommended edits beyond the mechanical pass:

- **Title page**: "IDA — A Reference Architecture for Time-Domain Audio/Video Looping" (the subtitle stays; "looping" remains accurate as the *primitive act* the architecture grows from, even though the product name no longer says "looper" — §1.7 already addresses this skeptic concern).
- **Abstract**: First paragraph gets one new sentence acknowledging the product identity. Example: *"This paper describes the architecture of **IDA — Idea Development Arranger**, the looping environment in the AutomagicArt instrument family alongside OTTO. IDA's central abstraction is that..."* Keep the original abstract's substance; just give the architecture a named home.
- **Closing manifesto (§18.4)**: untouched. The closing lines are perfect as-is and don't reference the product name.
- **Appendix C worked example**: untouched mechanically beyond the substitution pass.

Commit: `docs: bump whitepaper to V8 for IDA identity, preserve V7 in archive`.

### 1d. Manual follow-ups (things sed can't catch)

Inspect and adjust by hand:

- `app/CMakeLists.txt`: target name, `PRODUCT_NAME`, `BUNDLE_ID`. The bundle ID becomes `com.larryseyer.ida` (and `.host` variant for the plug-in host process). Binary name becomes `IDA` (produces `IDA.app`). Verify signing identifier strings in `--identifier` flags.
- `app/CMakeLists.txt` plist permission strings (microphone, camera, etc. — 5 access prompts): verify they read naturally with "IDA" rather than awkwardly. *"IDA needs access to your microphone to capture audio."* Replace any phrase that read fine with "Sirius Looper" but reads off with three letters.
- `bash/bu.sh`: backup directory name and zip filename. Operator should also rename the Dropbox folder `Sirius Looper Backup` → `IDA Backup` manually (out of repo).
- `bash/ralph.sh` header comment.
- `prd.json` top-level `"project"` field.
- `CLAUDE.md` (project): the `Sirius-Origin:` trailer convention now reads `Ida-Origin:`. Mention legacy `Sirius-Origin` trailers remain in git log for historical commits; audit grep becomes `git log --grep='Sirius-Origin\|Ida-Origin'`.
- Desktop alias: operator deletes the old `Sirius Looper` alias and recreates pointing at the new `IDA.app`.

### 1e. Verification

- `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build --target IdaTests && ctest --test-dir build` — baseline must match the pre-rename 449/450.
- `cmake --build build --target IDA` — produces `build/app/IDA_artefacts/Release/IDA.app`.
- Operator launches the .app, confirms window title, About box, file dialogs read "IDA" (or "IDA — Idea Development Arranger" in long-form contexts like the About box).
- `grep -rni 'sirius' --exclude-dir=build --exclude-dir=external --exclude-dir=.git --exclude-dir=docs/archive .` returns only:
  - Intentional historical references in commit messages (can't change git log)
  - Anything in `external/OTTO/` (handled in Phase 5)
  - Anything you decide is acceptable residue (flag each)

Merge `rename/ida` → `master` fast-forward. Push.

---

## Phase 2 — Website rename (website/ subdirectory)

The website is Eleventy, previously deployed via `.github/workflows/pages.yml` to the `gh-pages` branch with CNAME `siriuslooper.com`. Since the new canonical home is `automagicart.com/ida`, the deployment target is now an **operator decision**:

**Option A (recommended): Retire the standalone IDA website.** The IDA product page lives on automagicart.com going forward. Either:
- (A1) Move the website source content into the AutomagicArt website repo (wherever that lives), restructure as a `/ida` subdirectory, and deploy there. The IDA project repo's `website/` directory either gets archived or deleted.
- (A2) Keep the IDA project repo's `website/` source as draft/reference content but disable the GitHub Pages workflow (remove or rename `.github/workflows/pages.yml`).

**Option B: Continue deploying the IDA website standalone**, but as a subdirectory of automagicart.com (would require automagicart.com to support GitHub Pages routing or a reverse proxy — more complex). Not recommended for pre-launch.

**Option C: Deploy the IDA website on a temporary GitHub Pages URL** (`larryseyer.github.io/ida` or similar) with no custom domain, as a staging environment until the AutomagicArt /ida page is ready.

This plan assumes **Option A1**. Steps:

### 2a. Internal rename of website source (covered by Phase 1's mechanical pass; verify)

- `website/src/_data/site.json`: `title` → `IDA`, `domain` → `automagicart.com`, `url` → `https://automagicart.com/ida`, `github` slug → `larryseyer/ida`.
- `website/src/CNAME`: delete file (no longer needed; AutomagicArt's CNAME owns its domain).
- `website/src/_includes/base.njk`: the hardcoded ASCII brand-mark `<span class="brand-mark-accent">S</span>IRIUS<LOOPER>` — replace with whatever IDA brand-mark treatment you want. Options to consider:
  - `<I>DA` with the I as the accent letter (parallels OTTO's likely brand-mark convention).
  - `<I>DA — <I>dea <D>omain <A>rranger` with the backronym letters emphasized (clever, only works in large display).
  - Plain `IDA` with no accent — simpler, lets the three letters carry their own weight.
  Operator picks during implementation.
- `.github/workflows/pages.yml`: either delete the file (Option A1/A2) or update `cname: automagicart.com` if for some reason kept.
- `website/package.json`: `name`, `description`.
- Page copy in `index.njk`, `about.njk`, `features.njk`, `architecture.njk`, `404.njk`, `sitemap.njk`, `docs/user-guide.md`, `docs/whitepaper.md` — covered by the mechanical pass but skim each for awkward residue (e.g., places where "Sirius Looper" was used as a noun phrase that becomes weirdly terse with "IDA").

### 2b. Decide deployment fate (operator)

Make the call between A1, A2, A3, B, C. If A1, schedule the content migration as a separate workstream against the AutomagicArt website repo (out of scope for this plan). If A2, disable the workflow and commit. If C, keep the workflow but point its CNAME at the staging URL.

### 2c. Verification (if anything still deploys)

- If website still builds locally: `cd website && npx @11ty/eleventy --serve` → visit `localhost:8080`, confirm zero "Sirius" residue.
- `grep -rn 'sirius' website/` returns empty (case-insensitive).
- If deployed to staging or AutomagicArt: visit the live URL, click all nav links.

---

## Phase 3 — Domain handling (much simpler now)

No new domain purchase, no GitHub Pages cutover, no DNS changes for IDA.

### 3a. Old domains lapse

Per pre-flight step 4, both `siriuslooper.com` and `ottodrums.com` are already set to non-renew. No further action.

### 3b. AutomagicArt /ida page exists

Outside the scope of this repo's rename plan, but flagged as a dependency: the AutomagicArt website needs an `/ida` page before any public reference to `automagicart.com/ida` makes sense. Handle in the AutomagicArt repo's own session. Until that page exists, internal docs can reference the URL knowing it 404s, but external communications (App Store listings, social posts, press) should wait.

Add to this repo's `todo.md`:

```
### 2026-05-23 — automagicart.com/ida page
- Files: in the AutomagicArt website repo (separate session)
- What was deferred: building the IDA product page at /ida
- Why deferred: separate repo, separate session
- What's needed to finish: open a Claude session in the AutomagicArt repo,
  create /ida page with content from this repo's website/src/ as a draft
```

### 3c. (Skipped — no GitHub Pages cutover needed for IDA)

---

## Phase 4 — GitHub repo rename + local dir rename

### 4a. GitHub repo rename

On GitHub: Settings → repository name → `siriuslooper` → `ida` → Rename. GitHub **automatically** sets up a redirect from old URLs to new for clones, web links, and API calls. Existing clones continue to work via the redirect.

Note: `github.com/larryseyer/ida` is the target. If for any reason that's not available (extremely unlikely under a personal account namespace), fall back to `automagicart-ida`.

### 4b. Update remote on the local checkout

```
git remote set-url origin https://github.com/larryseyer/ida.git
git remote -v   # verify
git fetch origin
```

### 4c. Local directory rename

After confirming `master` is clean and pushed:

```
cd /Users/larryseyer
mv SiriusLooper IDA
```

Update operator-side references the agent can't reach:

- VS Code / Xcode workspaces / project window restore lists.
- The Desktop alias `Sirius Looper` (recreate from new build).
- The Claude Code project memory dir `/Users/larryseyer/.claude/projects/-Users-larryseyer-SiriusLooper/` — Claude Code keys session memory by directory path. After renaming the local dir, Claude will create a fresh project memory at `-Users-larryseyer-IDA`. Either (a) `mv` the old memory dir to match the new path so MEMORY.md follows, or (b) start fresh and re-pin key memories. Recommend (a).
- Any shell aliases, `cd` shortcuts, scheduled jobs, or ralph harness scripts (`run_ralph.sh` in the operator's other terminal) — update the path strings.
- The `bash/bu.sh` Dropbox destination string already updated in Phase 1.

### 4d. Verify

Clean build from the new path:

```
cd /Users/larryseyer/IDA
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA IdaTests
ctest --test-dir build
```

Push a trivial commit and confirm it lands on `larryseyer/ida`.

---

## Phase 5 — OTTO submodule + OTTO whitepaper + cross-project inbox

The OTTO submodule at `external/OTTO/` is its own repo. The mechanical pass in Phase 1 **must skip** that directory (it's a foreign codebase). But unlike the previous Sirius→Serious plan, IDA's footprint in OTTO is **larger** than just the inbox — the OTTO whitepaper has architectural references to Sirius Looper that must be updated to maintain document agreement.

### 5a. OTTO whitepaper updates

The OTTO whitepaper (`docs/OTTO_WhitePaper.md` in the OTTO repo) references Sirius Looper at the following touchpoints. Each needs updating:

- **Glossary entry** "Sirius Looper" → rename to "IDA" or restructure as "IDA (formerly Sirius Looper)". The entry text needs full rewrite to describe IDA's role as the host environment.
- **Mental Model §** Host and membrane — multiple references to Sirius as a hosting context.
- **Decision Rationale § Why OTTO is not the timepiece** — references Sirius's elevation of OTTO into the timepiece role.
- **MIDI surface § External MIDI contract** — references "Sirius Looper, for example, is UMP-native internally but downcasts to MIDI 1.0...".
- **Threading §** references to OTTO running "embedded-in-Sirius".
- **Source decisions** appendix — several slugs reference Sirius:
  - `feedback_pristine_audio_thread_golden_rule` — "in every plugin context including embedded-in-Sirius" → "embedded-in-IDA".
  - `project_otto_sirius_external_contract` — rename slug to `project_otto_ida_external_contract` and update description.

Slug renames in the source decisions appendix are *content* changes, not just text replacements, because operator memory references those slugs. The operator should decide whether to:
- (i) Rename slugs in place, accepting that any external reference to the old slug breaks.
- (ii) Add new IDA-named slugs as aliases, leaving old slugs as historical pointers.

Recommend (i) — cleaner, matches the full-rename posture of this plan.

### 5b. OTTO cross-project inbox

`external/OTTO/CROSS_PROJECT_INBOX.md`. IDA's Claude has full edit autonomy on this file per CLAUDE.md. Update:

- File header: `Cross-Project Inbox — OTTO ⇄ IDA`.
- All `[FROM SIRIUS → OTTO]` / `[FROM OTTO → SIRIUS]` headers → `IDA`.
- Entry format: `Sirius commit:` → `IDA commit:`.
- **Existing entries**: rewrite the directional labels and references to the new name. The entries themselves stay (they document real historical work).
- Add a new entry at the top documenting the rename, so OTTO's Claude sees it at next session start:

```markdown
## 2026-05-23 — Sirius Looper renamed to IDA

[FROM IDA → OTTO]

The looping environment formerly known as Sirius Looper has been renamed to
**IDA — Idea Development Arranger**, the companion to OTTO under the AutomagicArt
brand. All architectural commitments stand; the rename is purely identity.

- C++ namespace: `sirius::` → `ida::`
- Bundle ID: `com.larryseyer.siriuslooper` → `com.larryseyer.ida`
- Canonical URL: `automagicart.com/ida`
- Commit trailer: `Sirius-Origin:` → `Ida-Origin:`
- Whitepaper updates landed in OTTO this session — Glossary, Source decisions,
  Mental Model § Host and membrane, MIDI surface, Decision Rationale § D.6.

See `IDA_Naming_Decision.md` in the IDA repo for the full rename rationale
(brand-world structure, trademark notes, backronym).
```

### 5c. Commit trailer convention

Going forward, `Sirius-Origin:` → `Ida-Origin:`. Old trailers in OTTO's history stay (immutable). Audit-trail grep becomes:

```
git log --grep='Sirius-Origin\|Ida-Origin' --all
```

### 5d. Workflow

- In `external/OTTO/`, on a branch off OTTO's `main`, make:
  - The whitepaper edits (§5a)
  - The inbox edit (§5b)
  - Add the rename-notice entry to the inbox
- Commit each change separately for clean diff history:
  - `docs: rename Sirius Looper → IDA in OTTO whitepaper`
  - `docs: update CROSS_PROJECT_INBOX for IDA rename`
- Use trailer `Ida-Origin: <new-sha-after-phase-1>`. Chicken-and-egg: use `bootstrap-rename` as placeholder if the IDA-side commit isn't pushed yet; fix in a follow-on commit once both repos are aligned.
- Push OTTO.
- Back in the IDA repo, bump the submodule SHA, commit, push.

OTTO's own source code (CMake, C++, headers, JUCE setup) does not reference "Sirius" — confirmed via grep in the prior cross-project exploration. Nothing to rename inside OTTO besides the whitepaper and inbox.

---

## Phase 6 — larryseyer.com (separate repo, separate session)

The larryseyer.com site lives locally at:

- `~/larryseyer/larryseyer.github.io/` — the GitHub Pages source repo (deploys to `larryseyer.github.io` → custom domain `larryseyer.com`).
- `~/larryseyer/` — the parent folder where the repo and related assets live.

This plan does not inventory those (different repo, different scope). Handle in its own Claude Code session, with the working directory set to `~/larryseyer/larryseyer.github.io/`:

1. `grep -rni 'sirius' .` to find every reference. Also check `~/larryseyer/` for sibling assets (images, drafts, scripts) that mention the old name.
2. Repeat the Phase 1 mechanical pass (text substitutions + filename renames) scoped to that repo's content, but with the **same** find/replace table from §1a — same casing conventions apply.
3. Update any link from larryseyer.com → siriuslooper.com to point at `automagicart.com/ida`. Update any link to the GitHub repo from `larryseyer/siriuslooper` → `larryseyer/ida` (GitHub's auto-redirect covers cloning but link text should match reality).
4. If any larryseyer.com content references `ottodrums.com`, update it to `automagicart.com/otto` for the same reason (both old vanity domains are lapsing).
5. Commit, push, let GitHub Pages rebuild. Verify on larryseyer.com.

Add a note in this repo's `todo.md` so it doesn't get forgotten:

```
### 2026-05-23 — larryseyer.com rename
- Files: ~/larryseyer/larryseyer.github.io/ (and siblings in ~/larryseyer/)
- What was deferred: scrub "Sirius Looper" / "siriuslooper.com" / "ottodrums.com"
  / "larryseyer/siriuslooper" references from the personal site
- Why deferred: separate repo, separate session — keeps blast radius contained
- What's needed to finish: open a Claude session in ~/larryseyer/larryseyer.github.io/,
  grep, rename per IDA_Naming_Decision.md casing conventions, push
```

---

## Files / paths critical to the rename

- `CMakeLists.txt`, `app/CMakeLists.txt`, `ui/CMakeLists.txt`, `engine/CMakeLists.txt`, `core/CMakeLists.txt`, `host_process/CMakeLists.txt` — all CMake target names, bundle IDs, plist permissions, signing IDs.
- `ui/include/sirius/IdaPalette.h` (after rename: `ui/include/ida/IdaPalette.h`) and the entire `ui/include/sirius/` tree — directory + filename moves.
- `bash/bu.sh`, `bash/test-s7.sh`, `bash/smoke-persistence.sh`, `bash/autotest.sh`, `bash/ralph.sh` — paths and product strings.
- `app/SiriusLooper.macos.entitlements` → `app/IDA.macos.entitlements`, `host_process/sirius_plugin_host.entitlements` → `host_process/ida_plugin_host.entitlements`.
- `website/src/_data/site.json`, `website/src/CNAME` (delete), `website/src/_includes/base.njk`, all `website/src/*.njk` pages, `website/package.json`, `.github/workflows/pages.yml` (delete or disable per Phase 2 decision).
- `docs/Sirius_Looper_Whitepaper_V7.md` → `docs/IDA_Whitepaper_V8.md`, `docs/Sirius_Looper_User_Guide.md` → `docs/IDA_User_Guide.md`, `docs/design/sirius-*.md`, `docs/archive/*.md` (filename + preamble only), `docs/LICENSE-THIRD-PARTY.md` (legal language), `docs/RT_SAFETY_CONTRACT.md`.
- `README.md`, `CLAUDE.md`, `continue.md`, `todo.md`, `progress.txt`, `prd.json`.
- `external/OTTO/docs/OTTO_WhitePaper.md` and `external/OTTO/CROSS_PROJECT_INBOX.md` — the two files touched in the OTTO submodule per Phase 5.

---

## End-to-end verification checklist

After all phases:

- [ ] `grep -rni 'sirius' --exclude-dir=build --exclude-dir=.git --exclude-dir=external --exclude-dir=docs/archive /Users/larryseyer/IDA` returns only intentional residue (commit messages, archive content, OTTO submodule files outside our control). Each hit accounted for.
- [ ] `ctest --test-dir build` matches the 449/450 baseline.
- [ ] `IDA.app` launches; window title reads "IDA", About box reads "IDA — Idea Development Arranger" (or chosen long form), file dialogs read "IDA".
- [ ] `git remote -v` in the local checkout points at `larryseyer/ida`.
- [ ] OTTO's `OTTO_WhitePaper.md` references IDA at every architectural touchpoint (Glossary, Mental Model § Host and membrane, Decision Rationale § D.6, MIDI surface, Source decisions). Each "Sirius" reference accounted for and updated.
- [ ] OTTO's `CROSS_PROJECT_INBOX.md` carries the rename announcement, and the most recent commit there has an `Ida-Origin:` trailer.
- [ ] `pre-ida-rename` tag exists on the master branch (pre-rename snapshot for rollback).
- [ ] `siriuslooper.com` and `ottodrums.com` both have auto-renew disabled at their respective registrars.
- [ ] `automagicart.com/ida` is either live with content, or the operator has explicitly noted that the page is in-progress in the AutomagicArt repo (and `todo.md` reflects this).
- [ ] `continue.md` is refreshed for the next session, lead line: "**IDA — Idea Development Arranger** — rename complete from Sirius Looper. See tag `pre-ida-rename` for pre-rename state. Canonical home: `automagicart.com/ida`. Sibling product: OTTO."
- [ ] `todo.md` has two entries: "larryseyer.com references — handle in that repo's own session" and "automagicart.com/ida page — handle in AutomagicArt repo session".
- [ ] `IDA_Naming_Decision.md` is committed at the repo root or `docs/` (the decision record from the prior session).

---

## Rollback strategy

If anything goes sideways during the rename, the recovery path is:

1. `git reset --hard pre-ida-rename` on `master` (locally and force-push if you pushed bad state — coordinate with any other working copies).
2. `mv /Users/larryseyer/IDA /Users/larryseyer/SiriusLooper` to restore the local dir.
3. Rename the GitHub repo back via Settings.
4. Restore the OTTO submodule SHA to its pre-rename commit (in the OTTO repo, `git reset --hard <pre-rename-sha>` and force-push if necessary).
5. The two old domains lapsing is the only irreversible step — but since neither product launched, no one notices.

The `pre-ida-rename` tag is the single point of truth for rollback. Make sure it's pushed to origin before starting Phase 1.

---

## Notes on this plan vs. the earlier Sirius→Serious plan

The Sirius→Serious plan handled a same-shape rename (6-letter word for 7-letter word, similar casing throughout). This IDA rename has three differences that drove the changes:

1. **Three-letter acronym vs. word** — different casings needed for different code contexts (C++ namespaces, classes, macros, binary name, brand display). The conventions table at the top of this plan locks these in once.
2. **No new vanity domain** — Phase 3 is dramatically simpler. No DNS work, no GitHub Pages cutover, no SSL cert provisioning, no /www CNAME setup.
3. **OTTO whitepaper has architectural references to update** — the earlier plan said "OTTO source doesn't reference Sirius" which was true for code but missed the whitepaper. Phase 5 here is broader.

The structural shape (one branch, mechanical pass, manual follow-ups, verify, merge fast-forward, then phase-by-phase) is unchanged because the workflow is right.
