#!/bin/bash

# Ralph Watchdog - Monitors Ralph's progress and detects stalls
# Runs independently, logs issues, and can restart Ralph if needed

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRD_FILE="$SCRIPT_DIR/../prd.json"
PROGRESS_FILE="$SCRIPT_DIR/../progress.txt"
LOG_FILE="$SCRIPT_DIR/ralph_watchdog.log"
CHECK_INTERVAL=300   # 5 min — 8 sample points across the threshold window, half the polling load of 180s
STALL_THRESHOLD=5400 # 90 min = 2× new ITERATION_TIMEOUT (2700s) + 30 min safety margin.
                     # Margin absorbs Bug 170 stash-roundtrip overhead (clean iOS-sim + iOS-device
                     # build runs twice per build-touching story) and the watchdog's 300s polling
                     # latency. Floor is 2× iter timeout; floor + ≥1 polling-interval margin is
                     # the real target. Tightening below 5400 re-opens the 2026-04-25 abort
                     # regime where the loop self-killed mid-flight on BUG-03.

# Track state
LAST_COMMIT=""
LAST_COMMIT_TIME=0
STALL_ALERTED=false

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

get_completed_count() {
    awk '/^## Tasks/{f=1;next} /^## /{f=0} f && /^\[x\]/' "$PROGRESS_FILE" 2>/dev/null | wc -l | tr -d ' '
}

get_next_story() {
    local id title
    id=$(awk '/^## Tasks/{f=1;next} /^## /{f=0} f && /^\[ \]/{print $3; exit}' "$PROGRESS_FILE" 2>/dev/null)
    if [[ -z "$id" ]]; then
        echo "unknown"
        return
    fi
    title=$(jq -r --arg id "$id" '.tasks[] | select(.id==$id) | .title' "$PRD_FILE" 2>/dev/null)
    echo "${id}: ${title:-unknown}"
}

get_latest_commit() {
    git log --oneline -1 --format="%h" 2>/dev/null || echo ""
}

get_latest_commit_time() {
    git log -1 --format="%ct" 2>/dev/null || echo "0"
}

is_ralph_running() {
    pgrep -f "ralph.sh" > /dev/null 2>&1
}

is_claude_running() {
    pgrep -f "claude" > /dev/null 2>&1
}

check_progress() {
    local current_commit=$(get_latest_commit)
    local current_time=$(date +%s)
    local completed=$(get_completed_count)
    local total=$(jq '.tasks | length' "$PRD_FILE" 2>/dev/null || echo "0")
    local next=$(get_next_story)

    # Check if all done
    if [[ "$completed" == "$total" ]]; then
        log "✅ ALL STORIES COMPLETE! ($completed/$total)"
        log "Ralph has finished the refactor successfully."
        exit 0
    fi

    # Check for new commit
    if [[ "$current_commit" != "$LAST_COMMIT" ]]; then
        LAST_COMMIT="$current_commit"
        LAST_COMMIT_TIME=$(get_latest_commit_time)
        STALL_ALERTED=false
        log "📦 Progress: $completed/$total complete | Next: $next | Commit: $current_commit"
    else
        # No new commit - check for stall
        local time_since_commit=$((current_time - LAST_COMMIT_TIME))

        if [[ $time_since_commit -gt $STALL_THRESHOLD ]] && [[ "$STALL_ALERTED" == "false" ]]; then
            STALL_ALERTED=true
            log "⚠️  STALL DETECTED: No commits for $((time_since_commit / 60)) minutes"
            log "    Last story: $next"
            log "    Claude running: $(is_claude_running && echo 'yes' || echo 'NO')"
            log "    Ralph running: $(is_ralph_running && echo 'yes' || echo 'NO')"

            # Check for errors in recent output
            if [[ -f "$SCRIPT_DIR/ralph_output.log" ]]; then
                local errors=$(tail -100 "$SCRIPT_DIR/ralph_output.log" | grep -i "error\|failed\|exception" | tail -3)
                if [[ -n "$errors" ]]; then
                    log "    Recent errors found:"
                    echo "$errors" | while read line; do log "      $line"; done
                fi
            fi

            # Liveness gate: if ralph_output.log was updated in the last 600s,
            # Claude is still streaming — defer the kill regardless of commit
            # age. A live output stream is a stronger signal than commit
            # cadence; a slow story may simply not have committed yet.
            #
            # 600s (not 60s) because clang/lld linker phases produce ZERO
            # stdout while resolving symbols on the OTTO toolchain — TFLite +
            # sfizz + JUCE + otto-core + plugin link cycles can stay silent
            # for 5–15 minutes mid-iteration. The 60s threshold treated those
            # silent stretches as "claude is hung" and killed mid-link on
            # 2026-04-25. Gate must be wider than the longest expected silent
            # phase, not the polling interval.
            local output_mtime=0
            if [[ -f "$SCRIPT_DIR/ralph_output.log" ]]; then
                output_mtime=$(stat -f %m "$SCRIPT_DIR/ralph_output.log" 2>/dev/null || echo "0")
            fi
            local quiet_for=$((current_time - output_mtime))
            if [[ $quiet_for -lt 600 ]]; then
                log "    🟢 ralph_output.log updated ${quiet_for}s ago — Claude still streaming, deferring kill"
                STALL_ALERTED=false
                return
            fi

            # Dirty-tree gate: if the working tree shows real edits, that is
            # evidence ralph is doing work — just hasn't committed yet. Defer
            # kill regardless of commit cadence. Pairs with the liveness gate
            # above; both must fail before we kill. `git diff --quiet HEAD`
            # short-circuits on first modified tracked file, so it stays cheap
            # even when the tree has many edits.
            if ! git -C "$SCRIPT_DIR/.." diff --quiet HEAD 2>/dev/null; then
                local modified_count
                modified_count=$(git -C "$SCRIPT_DIR/.." diff --name-only HEAD 2>/dev/null | wc -l | tr -d ' ')
                log "    🟡 Working tree has ${modified_count} modified files — work in progress, deferring kill"
                STALL_ALERTED=false
                return
            fi

            # Cost-driven hard stop: stalled ralph burns API credits indefinitely.
            # Targeted kill of ralph.sh's process tree only — does NOT touch the
            # operator's interactive `claude` sessions. The pgrep pattern
            # "bash/ralph.sh" is narrow enough that ralph_watchdog.sh (with the
            # underscore) does not self-match.
            local ralph_pid
            ralph_pid=$(pgrep -f "bash/ralph.sh" | head -1)
            if [[ -n "$ralph_pid" ]]; then
                log "    🔪 KILLING ralph (PID $ralph_pid) and its children to stop credit burn"
                pkill -TERM -P "$ralph_pid" 2>/dev/null || true
                kill -TERM "$ralph_pid" 2>/dev/null || true
                sleep 2
                pkill -KILL -P "$ralph_pid" 2>/dev/null || true
                kill -KILL "$ralph_pid" 2>/dev/null || true
            else
                log "    ⚠️  Could not find ralph.sh process to kill (may have exited already)"
            fi
            log "    Watchdog exiting — operator must restart ralph deliberately."
            exit 1
        elif [[ $time_since_commit -le $STALL_THRESHOLD ]]; then
            log "⏳ Waiting: $completed/$total | Working on: $next | ${time_since_commit}s since last commit"
        fi
    fi
}

main() {
    log "=========================================="
    log "Ralph Watchdog Started"
    log "Check interval: ${CHECK_INTERVAL}s ($(($CHECK_INTERVAL / 60)) min)"
    log "Stall threshold: ${STALL_THRESHOLD}s ($(($STALL_THRESHOLD / 60)) min)"
    log "=========================================="

    # Initialize
    LAST_COMMIT=$(get_latest_commit)
    LAST_COMMIT_TIME=$(get_latest_commit_time)

    local completed=$(get_completed_count)
    local total=$(jq '.tasks | length' "$PRD_FILE" 2>/dev/null || echo "0")
    log "Starting state: $completed/$total tasks complete"

    while true; do
        check_progress
        sleep "$CHECK_INTERVAL"
    done
}

# Handle Ctrl+C gracefully
trap 'log "Watchdog stopped by user"; exit 0' SIGINT SIGTERM

main
