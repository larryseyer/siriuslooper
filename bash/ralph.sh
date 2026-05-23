#!/bin/bash
# Ralph Wiggum - Long-running AI agent loop for Sirius Looper.
# Operator-launched only. Claude never invokes this.
# Usage: ./bash/ralph.sh [max_iterations]

set -e

# Parse arguments
MAX_ITERATIONS=25
if [[ $# -gt 0 ]] && [[ "$1" =~ ^[0-9]+$ ]]; then
  MAX_ITERATIONS="$1"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/.."
PRD_FILE="$PROJECT_ROOT/prd.json"
PROGRESS_FILE="$PROJECT_ROOT/progress.txt"
ARCHIVE_DIR="$SCRIPT_DIR/archive"
LAST_GENERATED_AT_FILE="$SCRIPT_DIR/.last-generated-at"

# Regenerate progress.txt from prd.json's task list. Header block + one
# `[ ] <id> - <title>` line per task ordered by priority then id, plus the
# `## Codebase Patterns` and `## Log` sections that the loop appends to.
regenerate_progress_file() {
  local now
  now=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  {
    echo "# Sirius Looper Ralph Loop Progress"
    echo ""
    echo "## Status"
    echo "state: not_started"
    echo "current_task: $(jq -r '[.tasks[]] | sort_by(.priority, .id)[0].id // "none"' "$PRD_FILE")"
    echo "last_commit: none"
    echo "last_updated: $now"
    echo ""
    echo "## Tasks"
    jq -r '[.tasks[]] | sort_by(.priority, .id) | .[] | "[ ] \(.id) - \(.title)"' "$PRD_FILE"
    echo ""
    echo "## Codebase Patterns"
    echo ""
    echo "(Empty at generation. Loop session appends reusable gotcha patterns here as it learns them.)"
    echo ""
    echo "## Log"
  } > "$PROGRESS_FILE"
}

# Archive previous run when prd.json's generated_at changes. When the operator
# regenerates prd.json (new spec), the previous progress.txt is archived to
# bash/archive/<old-date>-<project-slug>/ and progress.txt is reset.
if [ -f "$PRD_FILE" ]; then
  CURRENT_GENERATED_AT=$(jq -r '.generated_at // empty' "$PRD_FILE" 2>/dev/null || echo "")
  LAST_GENERATED_AT=$(cat "$LAST_GENERATED_AT_FILE" 2>/dev/null || echo "")

  if [ -n "$CURRENT_GENERATED_AT" ] && [ -n "$LAST_GENERATED_AT" ] && [ "$CURRENT_GENERATED_AT" != "$LAST_GENERATED_AT" ]; then
    LAST_DATE=$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$LAST_GENERATED_AT" "+%Y-%m-%d" 2>/dev/null || echo "unknown-date")
    SLUG=$(jq -r '.project // "run"' "$PRD_FILE" | tr '[:upper:] ' '[:lower:]-')
    ARCHIVE_FOLDER="$ARCHIVE_DIR/$LAST_DATE-$SLUG"

    echo "Archiving previous run (generated_at $LAST_GENERATED_AT)"
    mkdir -p "$ARCHIVE_FOLDER"
    [ -f "$PROGRESS_FILE" ] && cp "$PROGRESS_FILE" "$ARCHIVE_FOLDER/"
    echo "   Archived progress.txt to: $ARCHIVE_FOLDER"

    regenerate_progress_file
    echo "   Reset progress.txt from new prd.json ($CURRENT_GENERATED_AT)"
  fi

  if [ -n "$CURRENT_GENERATED_AT" ]; then
    echo "$CURRENT_GENERATED_AT" > "$LAST_GENERATED_AT_FILE"
  fi
fi

# Initialize progress file if it doesn't exist
if [ ! -f "$PROGRESS_FILE" ]; then
  regenerate_progress_file
fi

# Rotate the iteration-output log
OUTPUT_LOG="$SCRIPT_DIR/ralph_output.log"
: > "$OUTPUT_LOG"

CLAUDE_MODEL="claude-opus-4-7"
CLAUDE_EFFORT="max"

# Per-iteration dollar cap. claude --print exits when this is reached.
RALPH_MAX_USD_PER_ITER="${RALPH_MAX_USD_PER_ITER:-35}"

# Per-iteration wall-clock cap. 45 min covers a clean cmake + ctest cycle on
# the Sirius codebase plus headroom for adapter implementation + tests.
ITERATION_TIMEOUT=2700
if command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT_CMD="gtimeout --kill-after=30 $ITERATION_TIMEOUT"
elif command -v timeout >/dev/null 2>&1; then
  TIMEOUT_CMD="timeout --kill-after=30 $ITERATION_TIMEOUT"
else
  echo "WARNING: neither gtimeout nor timeout found - per-iteration wall-clock cap DISABLED."
  echo "Install GNU coreutils (brew install coreutils) to enable the safety net."
  TIMEOUT_CMD=""
fi

# Refuse to run if learning-output-style plugin is enabled. It rewrites the
# inner `claude --print` to leave work "for operator review" instead of
# committing, emits ★ Insight banners, and never emits <promise>HALTED</promise>.
if [[ -f "$HOME/.claude/settings.json" ]] && \
   command -v jq >/dev/null 2>&1 && \
   jq -e '.enabledPlugins["learning-output-style@claude-plugins-official"] == true' \
        "$HOME/.claude/settings.json" >/dev/null 2>&1; then
  echo "ERROR: learning-output-style plugin is enabled in ~/.claude/settings.json."
  echo "       Ralph requires the default output style. Disable that plugin and re-run."
  exit 5
fi

echo "Starting Ralph (Sirius Looper) - Max iterations: $MAX_ITERATIONS"
echo "Per-iter dollar cap: \$$RALPH_MAX_USD_PER_ITER  (override with RALPH_MAX_USD_PER_ITER env var)"

# Track git HEAD between iterations. Two consecutive iters of zero git
# progress = exit 6 to stop the bleed.
LAST_COMMIT_SEEN=$(git -C "$PROJECT_ROOT" rev-parse HEAD 2>/dev/null || echo "none")
NO_COMMIT_ITERS=0
NO_COMMIT_THRESHOLD=1

for ((i=1; i<=MAX_ITERATIONS; i++)); do
  echo ""
  echo "==============================================================="
  echo "  Ralph Iteration $i of $MAX_ITERATIONS"
  echo "  Model: $CLAUDE_MODEL   Effort: $CLAUDE_EFFORT"
  echo "  Halt-protection: HALTED-grep ON, no-checkbox-flip threshold=3, no-commit threshold=$NO_COMMIT_THRESHOLD, per-iter cap \$$RALPH_MAX_USD_PER_ITER"
  echo "==============================================================="

  OUTPUT=$($TIMEOUT_CMD claude --dangerously-skip-permissions --model "$CLAUDE_MODEL" --effort "$CLAUDE_EFFORT" --max-budget-usd "$RALPH_MAX_USD_PER_ITER" --print "Follow the Ralph Agent Instructions in CLAUDE.md (project + user). Read prd.json, progress.txt, and continue.md, then implement the next unchecked task. STATE FILES: prd.json holds the immutable task spec under .tasks[] (each task keyed by .id like T01, with .title, .goal, .primary_files, .acceptance_criteria, .commit_message). progress.txt holds the mutable state — its '## Tasks' section lists '[ ] T01 - <title>' for unchecked and '[x] T01 - <title>' for done. continue.md is the human cold-boot handoff (the next operator session reads it to resume). Pick the first '[ ]' line under '## Tasks' as your task; look up its full spec in prd.json by .id. BUILD POLICY (Sirius-specific): Allowed verify command is 'cmake --build build --target SiriusTests && ctest --test-dir build --output-on-failure'. Compile-only builds and ctest ARE allowed and encouraged. Do NOT run anything that launches a GUI, installs to a device, or expects an operator: no 'open Sirius Looper.app', no iOS build/deploy, no Simulator launches, no AUv3 host launches — those stay with the operator outside the loop. If the next task is T01 (T3b-CMP) or T02 (T3c-DLY), perform the pre-flight read of the OTTO Player FX header named in the task spec FIRST and emit <promise>HALTED</promise> if the documented halt condition fires (sidechain bus / allocation in process()). COMMIT POLICY (mandatory, overrides default): When the task is complete you MUST in the SAME iteration: (a) flip its checkbox in progress.txt from '[ ] T0N - ...' to '[x] T0N - ...' (use sed -i.bak then rm the .bak), (b) update progress.txt header fields: 'last_commit:' to the new short SHA after committing, 'last_updated:' to the current ISO-UTC timestamp, 'current_task:' to the next unchecked task's id (or 'none' if all done), 'state:' to 'in_progress' (or 'complete' on the last task), (c) REFRESH continue.md: rewrite the top header block to announce the just-shipped task in the existing Sirius style (bold one-line 'DONE THIS SESSION' summary at top with task ID + 1-line impact + new ctest baseline + newest commit SHA placeholder; demote the prior head to '# (archived header — YYYY-MM-DD) — <prior one-liner>' mirroring the existing 2026-05-23 archive marker in continue.md; update the '▶ NEXT' section to point at the next '[ ]' task in progress.txt with a brief 'First moves' bullet list), (d) land all changes (source + progress.txt + continue.md) as a single git commit with: 'git add -A' then 'git commit -m \"<task.commit_message>\"' (SINGLE-LINE message — bu.sh derives a Dropbox zip filename from the message and a multi-line commit breaks the path), THEN 'git push origin master' (push is mandatory on Sirius — operator memory feedback_claude_commits_and_pushes_master authorizes it). After the push succeeds, return to update the continue.md commit-SHA placeholder if it was left as '<TBD>' — do this as a follow-up amend ONLY if you committed the placeholder; preferred path is to get the SHA into the commit on first try by running 'git log -1 --format=%h' on a parent commit and using a deterministic timestamp + then editing post-push if needed, but a stable placeholder is acceptable. Do NOT leave changes uncommitted 'for operator review.' The Claude Code default 'NEVER commit unless explicitly asked' rule is OVERRIDDEN for this loop: every iteration ends with HEAD advanced AND pushed OR with <promise>HALTED</promise> emitted and a ## HALT entry in todo.md per CLAUDE.md hard-stop rules. An iteration that flips a checkbox without committing-and-pushing is a defect — the no-commit gate will exit on the first such iteration." 2>&1 | tee /dev/stderr | tee -a "$OUTPUT_LOG") || true

  # Detect timeout-killed iteration.
  if [[ -n "$TIMEOUT_CMD" ]] && [[ -z "${OUTPUT// /}" ]]; then
    echo ""
    echo "==============================================================="
    echo "  Iteration $i produced ZERO output (likely timeout-killed at ${ITERATION_TIMEOUT}s)."
    echo "  Aborting loop to prevent runaway credit burn."
    echo "==============================================================="
    exit 4
  fi

  # No-commit gate
  CURRENT_COMMIT=$(git -C "$PROJECT_ROOT" rev-parse HEAD 2>/dev/null || echo "none")
  if [[ "$CURRENT_COMMIT" == "$LAST_COMMIT_SEEN" ]]; then
    NO_COMMIT_ITERS=$((NO_COMMIT_ITERS + 1))
    echo "  ⚠️  No new commit this iteration ($NO_COMMIT_ITERS/$NO_COMMIT_THRESHOLD)"
    if [[ $NO_COMMIT_ITERS -ge $NO_COMMIT_THRESHOLD ]]; then
      echo ""
      echo "==============================================================="
      echo "  $NO_COMMIT_ITERS consecutive iterations without a new git commit."
      echo "  Loop is producing output but not landing changes — exiting to"
      echo "  save tokens. Check bash/ralph_output.log tail for the reason."
      echo "==============================================================="
      exit 6
    fi
  else
    NO_COMMIT_ITERS=0
    LAST_COMMIT_SEEN="$CURRENT_COMMIT"
  fi

  # Hard-stop on Claude-emitted HALT
  if echo "$OUTPUT" | tail -50 | grep -q "<promise>HALTED</promise>"; then
    echo ""
    echo "==============================================================="
    echo "  Ralph emitted HALTED at iteration $i. Stopping loop."
    echo "  See todo.md for the ## HALT entry and resolve before re-running."
    echo "==============================================================="
    exit 2
  fi

  # Authoritative termination check
  COMPLETED=$(awk '/^## Tasks/{f=1;next} /^## /{f=0} f && /^\[x\]/' "$PROGRESS_FILE" 2>/dev/null | wc -l | tr -d ' ')
  TOTAL=$(jq '.tasks | length' "$PRD_FILE" 2>/dev/null || echo "0")

  # Stagnation guard: three consecutive iters with no new checkbox flip
  if [[ -n "$PREV_COMPLETED" ]] && [[ "$COMPLETED" == "$PREV_COMPLETED" ]]; then
    STAGNANT_ITERS=$((STAGNANT_ITERS + 1))
    if [[ $STAGNANT_ITERS -ge 3 ]]; then
      echo ""
      echo "==============================================================="
      echo "  No progress for 3 iterations (stuck on: $COMPLETED/$TOTAL)."
      echo "  Stopping loop to prevent token waste."
      echo "==============================================================="
      exit 3
    fi
  else
    STAGNANT_ITERS=0
  fi
  PREV_COMPLETED="$COMPLETED"

  if [[ "$COMPLETED" == "$TOTAL" ]] && [[ "$TOTAL" -gt 0 ]]; then
    echo ""
    echo "==============================================================="
    echo "  All $TOTAL tasks checked off! Starting verification phase..."
    echo "==============================================================="

    VERIFY_OUTPUT=$($TIMEOUT_CMD claude --dangerously-skip-permissions --model "$CLAUDE_MODEL" --effort "$CLAUDE_EFFORT" --max-budget-usd "$RALPH_MAX_USD_PER_ITER" --print "All $TOTAL tasks are now marked [x] in progress.txt's '## Tasks' section. Run full verification: 'cmake --build build --target SiriusTests && ctest --test-dir build --output-on-failure'. If green, set progress.txt 'state:' header to 'complete', refresh continue.md one final time to mark the loop complete and queue T3d-RVB as the next operator design session, commit + push, then output <promise>COMPLETE</promise>. If tests fail, fix the issues, COMMIT the fixes as a single git commit with a SINGLE-LINE message in the form 'fix: <short title>', push, and re-run tests until they pass. The Claude Code default 'NEVER commit unless explicitly asked' rule is OVERRIDDEN for this loop." 2>&1 | tee /dev/stderr | tee -a "$OUTPUT_LOG") || true

    if echo "$VERIFY_OUTPUT" | grep -q "<promise>COMPLETE</promise>"; then
      echo ""
      echo "Ralph completed all tasks with verification!"
      echo "Completed at iteration $i of $MAX_ITERATIONS"
      exit 0
    fi

    echo "Verification found issues. Continuing to fix..."
  fi

  echo "Iteration $i complete. Continuing..."
  sleep 2
done

echo ""
echo "Ralph reached max iterations ($MAX_ITERATIONS) without completing all tasks."
echo "Check $PROGRESS_FILE for status."
exit 1
