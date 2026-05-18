#!/usr/bin/env bash
# Populate external/ with vendored third-party dependencies as plain snapshots
# (no nested .git). external/ is gitignored — deps are not committed. This
# matches the sister app OTTO's layout.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXT="$ROOT/external"
mkdir -p "$EXT"

# name|repo|ref
DEPS=(
    "JUCE|https://github.com/juce-framework/JUCE.git|8.0.12"
    "Catch2|https://github.com/catchorg/Catch2.git|v3.15.0"
    "soxr|https://github.com/chirlu/soxr.git|master"  # libsoxr is effectively frozen at 0.1.3
    "clap|https://github.com/free-audio/clap.git|1.2.6"  # CLAP plug-in SDK (header-only)
)

for entry in "${DEPS[@]}"; do
    IFS='|' read -r name repo ref <<< "$entry"
    dest="$EXT/$name"
    if [ -d "$dest" ]; then
        echo "external/$name already present — skipping"
        continue
    fi
    echo "cloning $name @ $ref ..."
    git clone --depth 1 --branch "$ref" -q "$repo" "$dest"
    rm -rf "$dest/.git"

    # Apply any vendored patches for this dependency (patches/<name>-*.patch).
    for patch in "$ROOT"/patches/"$name"-*.patch; do
        [ -e "$patch" ] || continue
        echo "  applying $(basename "$patch")"
        patch -p1 -d "$dest" < "$patch"
    done
done

echo "external/ ready."
