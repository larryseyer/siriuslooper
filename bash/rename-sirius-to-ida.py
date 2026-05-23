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
