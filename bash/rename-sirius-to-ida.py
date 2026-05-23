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
