#!/bin/bash
# Ralph Suite Launcher - Starts loop, monitor, and watchdog together.
# Operator-launched only. Claude never invokes this (per ~/.claude/CLAUDE.md).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

RALPH_ARGS="$@"

cleanup() {
    echo "Stopping Ralph suite..."
    kill $MONITOR_PID $WATCHDOG_PID 2>/dev/null
    wait
    exit 0
}
trap cleanup SIGINT SIGTERM

bash/ralph_watchdog.sh &
WATCHDOG_PID=$!
echo "Started watchdog (PID: $WATCHDOG_PID)"

osascript -e 'tell app "Terminal" to do script "cd '"$SCRIPT_DIR"' && bash/ralph_monitor.sh"' &
MONITOR_PID=$!
echo "Started monitor in new Terminal tab"

bash/ralph.sh $RALPH_ARGS

cleanup
