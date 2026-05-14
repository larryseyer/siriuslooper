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
done

echo "external/ ready."
