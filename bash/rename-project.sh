#!/usr/bin/env bash
# rename-project.sh — migrate /Users/larryseyer/Sirius Looper → /Users/larryseyer/SiriusLooper
#
# Why: the space in the folder name forces shell escaping (Sirius\ Looper), and the
# escaped form does not match the quoted form for Claude Code's permission allowlist,
# producing duplicate prompts. Removing the space eliminates that whole class of friction.
#
# What stays: the product / .app bundle name "Sirius Looper". This script only rewrites
# the absolute path substring "/Users/larryseyer/Sirius Looper" (and its backslash-escaped
# form). Product-name references in CMake, plist text, app bundle filename, etc. are left
# untouched.
#
# Run this from a Terminal tab OUTSIDE the project tree, after quitting Claude Code.

set -euo pipefail

OLD_PATH="/Users/larryseyer/Sirius Looper"
NEW_PATH="/Users/larryseyer/SiriusLooper"
OLD_MEM="$HOME/.claude/projects/-Users-larryseyer-Sirius-Looper"
NEW_MEM="$HOME/.claude/projects/-Users-larryseyer-SiriusLooper"

DRY_RUN=0
ASSUME_YES=0
FORCE=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [--dry-run] [--yes] [--force]

  --dry-run   Print every action; change nothing.
  --yes       Skip per-file confirmation during path rewrite.
  --force     Proceed despite lsof warnings (cannot bypass git-clean / no-claude checks).
EOF
}

for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        --yes)     ASSUME_YES=1 ;;
        --force)   FORCE=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown flag: $arg" >&2; usage; exit 2 ;;
    esac
done

say()  { printf '[rename] %s\n' "$*"; }
warn() { printf '[rename] WARN: %s\n' "$*" >&2; }
die()  { printf '[rename] FATAL: %s\n' "$*" >&2; exit 1; }
run()  {
    if [[ $DRY_RUN -eq 1 ]]; then
        printf '[dry-run] %s\n' "$*"
    else
        eval "$@"
    fi
}

# ---------------------------------------------------------------------------
# Pre-flight
# ---------------------------------------------------------------------------

say "Pre-flight checks..."

[[ -d "$OLD_PATH" ]] || die "Source dir not found: $OLD_PATH"
[[ -e "$NEW_PATH" ]] && die "Target already exists: $NEW_PATH"
[[ -e "$NEW_MEM"  ]] && die "Target memory dir already exists: $NEW_MEM"

# Git clean
GIT_STATUS=$(git -C "$OLD_PATH" status --porcelain 2>/dev/null || true)
if [[ -n "$GIT_STATUS" ]]; then
    echo "[rename] FATAL: git working tree is not clean. Commit or stash first:" >&2
    echo "$GIT_STATUS" | sed 's/^/    /' >&2
    exit 1
fi

# No Claude Code running. Matching is conservative — any process whose argv starts
# with "claude" and is not THIS script.
SELF_PID=$$
CLAUDE_PIDS=$(pgrep -fl 'claude' 2>/dev/null | awk -v me="$SELF_PID" '$1 != me && $2 ~ /(^|\/)claude($|[^a-zA-Z])/ {print $1}' || true)
if [[ -n "$CLAUDE_PIDS" ]]; then
    warn "Claude Code appears to be running (PIDs: $(echo $CLAUDE_PIDS | tr '\n' ' '))."
    warn "Quit Claude Code before continuing. Listing matching processes:"
    pgrep -fl 'claude' | awk -v me="$SELF_PID" '$1 != me {print "    " $0}' >&2
    die  "Refusing to rename a directory currently in use by Claude Code."
fi

# lsof — best-effort; informational unless --force not given
if command -v lsof >/dev/null 2>&1; then
    LSOF_HITS=$(lsof +D "$OLD_PATH" 2>/dev/null | tail -n +2 || true)
    if [[ -n "$LSOF_HITS" ]]; then
        warn "Files in the tree are held open by other processes:"
        echo "$LSOF_HITS" | head -20 | sed 's/^/    /' >&2
        if [[ $FORCE -eq 0 ]]; then
            die "Close those processes or re-run with --force."
        fi
        warn "Proceeding despite open handles (--force)."
    fi
fi

say "Pre-flight OK."

# ---------------------------------------------------------------------------
# Operations
# ---------------------------------------------------------------------------

say "Step 1/6: rename project directory"
say "  $OLD_PATH"
say "  -> $NEW_PATH"
run "mv \"$OLD_PATH\" \"$NEW_PATH\""

say "Step 2/6: rename Claude Code memory directory"
say "  $OLD_MEM"
say "  -> $NEW_MEM"
if [[ -d "$OLD_MEM" ]]; then
    run "mv \"$OLD_MEM\" \"$NEW_MEM\""
else
    warn "Memory dir not found at $OLD_MEM (already moved? skipping)"
fi

WORK_ROOT="$NEW_PATH"
[[ $DRY_RUN -eq 1 ]] && WORK_ROOT="$OLD_PATH"  # dry-run: read from old location

say "Step 3/6: scan for hardcoded path occurrences"
# Two forms: literal-with-space, and backslash-escaped.
# Scoped to file types that can legitimately contain absolute paths.
HITS_FILE=$(mktemp)
trap 'rm -f "$HITS_FILE"' EXIT

(
    cd "$WORK_ROOT"
    grep -rln \
        --include='*.sh' --include='*.json' --include='*.yml' --include='*.yaml' \
        --include='*.cmake' --include='*.txt' --include='*.toml' --include='*.cfg' \
        --include='CMakeLists.txt' \
        -e '/Users/larryseyer/Sirius Looper' \
        -e '/Users/larryseyer/Sirius\\ Looper' \
        . 2>/dev/null || true
) > "$HITS_FILE"

HIT_COUNT=$(wc -l < "$HITS_FILE" | tr -d ' ')
if [[ "$HIT_COUNT" -eq 0 ]]; then
    say "  No path occurrences found. Nothing to rewrite."
else
    say "  Found $HIT_COUNT file(s) with hardcoded paths:"
    sed 's/^/    /' "$HITS_FILE"
fi

say "Step 4/6: rewrite paths in affected files"
if [[ "$HIT_COUNT" -gt 0 ]]; then
    while IFS= read -r rel; do
        target="$WORK_ROOT/${rel#./}"
        if [[ $ASSUME_YES -eq 0 && $DRY_RUN -eq 0 ]]; then
            printf '  Rewrite %s? [y/N] ' "$rel"
            read -r ans </dev/tty
            [[ "$ans" =~ ^[Yy]$ ]] || { warn "  skipped $rel"; continue; }
        fi
        # Two sed expressions: literal-with-space, then backslash-escaped.
        # Use a delimiter that does not appear in the paths.
        if [[ $DRY_RUN -eq 1 ]]; then
            printf '[dry-run]   sed-rewrite %s\n' "$rel"
        else
            sed -i.bak \
                -e 's|/Users/larryseyer/Sirius Looper|/Users/larryseyer/SiriusLooper|g' \
                -e 's|/Users/larryseyer/Sirius\\ Looper|/Users/larryseyer/SiriusLooper|g' \
                "$target"
            rm -f "${target}.bak"
            say "  rewrote $rel"
        fi
    done < "$HITS_FILE"
fi

say "Step 5/6: wipe stale build artifacts"
for d in build build-xcode; do
    if [[ -d "$WORK_ROOT/$d" ]]; then
        run "rm -rf \"$WORK_ROOT/$d\""
        say "  removed $d/"
    fi
done

say "Step 6/6: post-rename verification"
if [[ $DRY_RUN -eq 0 ]]; then
    SURVIVORS=$(
        cd "$NEW_PATH"
        grep -rln \
            --include='*.sh' --include='*.json' --include='*.yml' --include='*.yaml' \
            --include='*.cmake' --include='*.txt' --include='*.toml' --include='*.cfg' \
            --include='CMakeLists.txt' \
            -e '/Users/larryseyer/Sirius Looper' \
            -e '/Users/larryseyer/Sirius\\ Looper' \
            . 2>/dev/null || true
    )
    if [[ -n "$SURVIVORS" ]]; then
        warn "Path string still present in:"
        echo "$SURVIVORS" | sed 's/^/    /' >&2
        warn "Inspect manually. (If these are deliberate references — e.g. inside a doc — leave them.)"
    else
        say "  No surviving path references. Clean."
    fi
    say "  git status:"
    git -C "$NEW_PATH" status --short | sed 's/^/    /' || true
fi

# ---------------------------------------------------------------------------
# Next steps
# ---------------------------------------------------------------------------

cat <<EOF

[rename] Done.

Next steps:
  1. cd /Users/larryseyer/SiriusLooper
  2. claude          # re-launch Claude Code at the new path
  3. bash bash/autotest.sh   # confirm toolchain works against the new path
  4. git add -u && git add bash/rename-project.sh
     git commit -m "chore: rename project root Sirius Looper -> SiriusLooper"

Notes:
  - The .app bundle is still named "Sirius Looper.app". That is intentional;
    only filesystem paths changed.
  - .claude/settings.local.json permission grants are now keyed to the new
    path. A few commands may re-prompt once and then be remembered.
  - Auto-memory is preserved under ~/.claude/projects/-Users-larryseyer-SiriusLooper/.

EOF
