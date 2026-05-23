#!/usr/bin/env python3
"""
One-shot mechanical rename pass: Sirius Looper -> IDA.

Implements the 26-row substitution table from IDA_Rename_Plan.md §1a in
exact longest-match-first order. Uses Python str.replace (literal, no
regex), reads tracked files via `git ls-files` (auto-respects .gitignore
and treats external/OTTO/ as a single submodule pointer rather than
descending into it).

Skips:
  - external/OTTO/  (Phase 5 handles this)
  - docs/archive/   (Phase 1c — preambles, not substitution)
  - The two IDA_*.md spec files (they ARE the spec; "Sirius" in find columns is intentional)
  - This script itself
  - Any file detected as binary (NUL byte in first 8KB)
  - Build artefacts (already gitignored, so absent from git ls-files)

Deleted on merge of rename/ida -> master per the plan.
"""

import os
import subprocess
import sys

# Order matters: longest match first. See IDA_Rename_Plan.md §1a.
SUBS = [
    ("Sirius Looper Backup", "IDA Backup"),
    ("Sirius Looper — Idea Development Arranger", "IDA — Idea Development Arranger"),
    ("Sirius Looper", "IDA"),
    ("siriuslooper.com", "automagicart.com/ida"),
    ("SiriusLookAndFeel", "IdaLookAndFeel"),
    ("SiriusBinaryData", "IdaBinaryData"),
    ("SiriusPalette", "IdaPalette"),
    ("SiriusEngine", "IdaEngine"),
    ("SiriusCore", "IdaCore"),
    ("SiriusVideo", "IdaVideo"),
    ("SiriusNet", "IdaNet"),
    ("SiriusTests", "IdaTests"),
    ("SiriusUi", "IdaUi"),
    ("SiriusLooper", "IDA"),
    ("siriuslooper", "ida"),
    ("SIRIUS_", "IDA_"),
    ("Sirius::", "Ida::"),
    ("sirius::", "ida::"),
    ("sirius_plugin_host", "ida_plugin_host"),
    ("sirius-smoke", "ida-smoke"),
    (".sirius.json", ".ida.json"),
    ("Sirius-Origin:", "Ida-Origin:"),
    ("Sirius/", "Ida/"),
    ("/sirius/", "/ida/"),
    ('"Sirius"', '"IDA"'),
    (" Sirius ", " IDA "),
    # Addendum: filename references the original §1a table missed (live files only;
    # archive filenames handled by Task 4 after enumeration). These point at the
    # new whitepaper V8 file + renamed design docs that Task 2 git-mv's into place.
    ("Sirius_Looper_Whitepaper_V7.md", "IDA_Whitepaper_V8.md"),
    ("Sirius_Looper_User_Guide.md", "IDA_User_Guide.md"),
    ("sirius-internal-fx.md", "ida-internal-fx.md"),
    ("sirius-colour-method.md", "ida-colour-method.md"),
    # Stale references to a long-deleted file `Sirius_Looper.md` (consolidated into V7
    # per project_whitepaper_path memory). Point them at the new V8 for consistency.
    ("Sirius_Looper.md", "IDA_Whitepaper_V8.md"),
    # Possessive form the spec's row 26 (whitespace-bounded) missed.
    ("Sirius's", "IDA's"),
    # Directional labels in the cross-project inbox protocol referenced in CLAUDE.md.
    ("[FROM SIRIUS → OTTO]", "[FROM IDA → OTTO]"),
    ("[FROM OTTO → SIRIUS]", "[FROM OTTO → IDA]"),
    ("Sirius ⇄ OTTO", "IDA ⇄ OTTO"),
    ("Sirius-originated", "IDA-originated"),
    # Include path patterns the spec's row 24 (`/sirius/`) missed: C++ include
    # statements use `"sirius/Foo.h"` and `<sirius/Foo.h>` (no leading slash).
    ('"sirius/', '"ida/'),
    ("<sirius/", "<ida/"),
    # CMake-target second pass: layer library names the §1a table didn't enumerate.
    # These pair with already-renamed `Ida::` aliases (rows 5-13 of original table).
    ("SiriusAppCore", "IdaAppCore"),
    ("SiriusAudio", "IdaAudio"),
    ("SiriusHost", "IdaHost"),
    ("SiriusPersistence", "IdaPersistence"),
    # CMake variables and external keychain profile name.
    ("sirius_clap_loader_src", "ida_clap_loader_src"),
    # Keychain profile: requires operator to re-run `xcrun notarytool store-credentials
    # ida-notary --apple-id itunes@larryseyer.com --team-id RR5DY39W4Q` before notarization
    # works again. Documented in todo.md as an operator action.
    ("sirius-notary", "ida-notary"),
    # CMake COMMENT prefix in user-visible build progress messages.
    ('"Sirius:', '"IDA:'),
    # Synthetic CLAP test-plugin bundle IDs — 26 refs across test fixtures, test code,
    # MainComponent demo wiring, and CMake. Bundle ID in plist + ID in plugin's self-
    # declaration + test expectations all flip together or scanner tests break.
    ("com.sirius.synthetic.test", "com.ida.synthetic.test"),
    ("com.sirius.synthetic.identity", "com.ida.synthetic.identity"),
    ("com.sirius.synthetic.statefulsynth", "com.ida.synthetic.statefulsynth"),
    # BUILD-CRITICAL: C++ namespace declarations the §1a table missed (table only
    # caught `sirius::` usages). Without this the 188 files with `namespace sirius
    # { ... }` would not compile against the substituted `ida::` references.
    ("namespace sirius", "namespace ida"),
    ("} // namespace sirius", "} // namespace ida"),
    # XPC bridge / GUI extern-C symbols (host_process). Defined in gui_cocoa.mm,
    # declared and called in main.cpp — must flip together as one group.
    ("sirius_appkit_init", "ida_appkit_init"),
    ("sirius_appkit_drain_events", "ida_appkit_drain_events"),
    ("sirius_gui_set_state", "ida_gui_set_state"),
    ("sirius_gui_show", "ida_gui_show"),
    ("sirius_gui_hide", "ida_gui_hide"),
    ("sirius_gui_resize", "ida_gui_resize"),
    # POSIX shared-memory channel name prefix the parent + child agree on.
    ("/sirius.", "/ida."),
    # Session-file envelope format key (writer + reader must agree). Pre-launch
    # dev iteration, no committed session JSON files with this key, so rename is
    # clean — operator's local sessions would need a re-save (writes the new key).
    ("sirius_version", "ida_version"),
    # Test scratch-file prefixes — internal-only.
    ("sirius-inputmixer-", "ida-inputmixer-"),
    ("sirius-finalize-", "ida-finalize-"),
    ("sirius-nondestructive-", "ida-nondestructive-"),
    ("sirius-defaults-", "ida-defaults-"),
    ("sirius-audio-dsp-", "ida-audio-dsp-"),
    # CLAUDE.md placeholder text for commit SHA references.
    ("<sirius-sha>", "<ida-sha>"),
    # Placeholder URL in plugin host descriptor.
    ("example.invalid/sirius", "example.invalid/ida"),
    # Obj-C class name in the host_process GUI bridge (4 refs in gui_cocoa.mm).
    ("SiriusPluginWindowDelegate", "IdaPluginWindowDelegate"),
    # Test-fixture plugin display names (visible in scanner output).
    ("Sirius Synthetic Identity", "IDA Synthetic Identity"),
    ("Sirius Stateful Synth Fixture", "IDA Stateful Synth Fixture"),
    # Hyphenated compound the spec row 3 missed (`Sirius Looper` with hyphens).
    ("Sirius-Looper-compatible", "IDA-compatible"),
    # Sirius Archive Format → IDA Archive Format; the SAF acronym therefore retires
    # in favor of IAF (matches the new expansion). Used in whitepaper, plan docs,
    # continue.md, and one comment in core/include/ida/InputKind.h.
    ("Sirius Archive Format (SAF)", "IDA Archive Format (IAF)"),
    ("IDA Archive Format (SAF)", "IDA Archive Format (IAF)"),
    # SAF → IAF (Sirius/IDA Archive Format acronym). DANGER: this substring
    # match damaged 40 files of `RT_SAFETY_CONTRACT` → `RT_IAFETY_CONTRACT` on
    # first run; that damage was reverted post-hoc. If re-running this script,
    # ALSO grep for `IAFETY` after the pass and revert any new instances. The
    # row is left in for record but commented out — any future SAF residue
    # should be hand-fixed.
    # ("SAF", "IAF"),  # DANGER — see comment above
    # URL-encoded link targets in README (legacy archive whitepaper links pointing
    # at deleted-then-renamed files).
    ("Sirius%20Looper%20Whitepaper%20V7.md", "IDA_Whitepaper_V8.md"),
    ("Sirius%20Looper%20Whitepaper%20V6.md", "archive/IDA_Whitepaper_V6_archive.md"),
    ("Sirius%20Looper%20Whitepaper%20V2.md", "archive/IDA_Whitepaper_V2_archive.md"),
    ("Sirius%20Looper%20Whitepaper%20V1.md", "archive/IDA_Whitepaper_V1_archive.md"),
    ("Sirius%20Looper%20User%20Guide.md", "IDA_User_Guide.md"),
    # CLAUDE.md cross-project inbox spec — directional and column-header text.
    ("Direction: SIRIUS → OTTO", "Direction: IDA → OTTO"),
    ("(or OTTO → SIRIUS)", "(or OTTO → IDA)"),
    ("Sirius commit:", "IDA commit:"),
    # CLAUDE.md prose lines that use bare "Sirius" as a synonym for the product.
    ("Back in Sirius:", "Back in IDA:"),
    ("push Sirius", "push IDA"),
    # Other bare-Sirius prose hits we know about (website docs + a VideoTape comment).
    ("Sirius, like its sister app OTTO", "IDA, like its sister app OTTO"),
    ("tape in Sirius", "tape in IDA"),
    ("do with Sirius", "do with IDA"),
    # Test scratch file prefixes beyond the InputMixer set already in earlier rows.
    ("sirius-shm-", "ida-shm-"),
    ("sirius-tapewriter-", "ida-tapewriter-"),
    ("sirius-flush-latency-", "ida-flush-latency-"),
    ("sirius-flac-test-", "ida-flac-test-"),
    ("sirius-tapestore-test-", "ida-tapestore-test-"),
    ("sirius-wet-", "ida-wet-"),
    ("sirius_clap_empty", "ida_clap_empty"),
    ("sirius_clap_bad", "ida_clap_bad"),
    # Idiomatic phrases throughout adapter headers + tests + design docs.
    ("Sirius-side", "IDA-side"),
    ("every copy of Sirius", "every copy of IDA"),
    ("transport bpm Sirius", "transport bpm IDA"),
    ("Sirius and OTTO", "IDA and OTTO"),
    ("<Sirius>/", "<IDA>/"),
]

EXCLUDE_PATH_PREFIXES = (
    "external/OTTO",
    "docs/archive/",
    ".claude/",
    ".cache/",
)

EXCLUDE_FILES = {
    "IDA_Naming_Decision.md",
    "IDA_Rename_Plan.md",
    "bash/rename-sirius-to-ida.py",
}


def should_skip(path: str) -> bool:
    if path in EXCLUDE_FILES:
        return True
    for prefix in EXCLUDE_PATH_PREFIXES:
        if path == prefix.rstrip("/") or path.startswith(prefix):
            return True
    return False


def is_binary(path: str) -> bool:
    try:
        with open(path, "rb") as fh:
            chunk = fh.read(8192)
        return b"\x00" in chunk
    except OSError:
        return True


def collect_candidates() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files"], capture_output=True, text=True, check=True
    )
    paths = [p for p in result.stdout.splitlines() if p]
    return [
        p
        for p in paths
        if not should_skip(p) and os.path.isfile(p) and not is_binary(p)
    ]


def apply_subs(files: list[str]) -> int:
    total = 0
    for find, replace in SUBS:
        touched = 0
        for path in files:
            try:
                with open(path, "r", encoding="utf-8") as fh:
                    content = fh.read()
            except (UnicodeDecodeError, OSError):
                continue
            if find not in content:
                continue
            new = content.replace(find, replace)
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(new)
            touched += 1
        total += touched
        print(f"  [{find!r:50} -> {replace!r:45}] {touched:4d} files")
    return total


def main() -> int:
    files = collect_candidates()
    print(f"Candidate files: {len(files)}")
    print(f"Running {len(SUBS)} substitution rows...\n")
    total = apply_subs(files)
    print(f"\nTotal file-edits across all rows: {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
