#!/bin/bash

# Ralph Status Monitor - Live terminal dashboard for the Ralph loop
# Reads from prd.json and progress.txt to show actual progress
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/.."
PRD_FILE="$PROJECT_ROOT/prd.json"
PROGRESS_FILE="$PROJECT_ROOT/progress.txt"
REFRESH_INTERVAL=5

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m'

# Clear screen and hide cursor
clear_screen() {
    clear
    printf '\033[?25l'  # Hide cursor
}

# Show cursor on exit
show_cursor() {
    printf '\033[?25h'  # Show cursor
}

# Cleanup function
cleanup() {
    show_cursor
    echo
    echo "Monitor stopped."
    exit 0
}

# Set up signal handlers
trap cleanup SIGINT SIGTERM EXIT

# Main display function
display_status() {
    clear_screen

    # Header
    echo -e "${WHITE}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${WHITE}║                           RALPH MONITOR                                ║${NC}"
    echo -e "${WHITE}║                        Live Status Dashboard                           ║${NC}"
    echo -e "${WHITE}╚════════════════════════════════════════════════════════════════════════╝${NC}"
    echo

    # PRD Status section
    if [[ -f "$PRD_FILE" ]]; then
        local branch=$(git -C "$PROJECT_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
        local total=$(jq '.tasks | length' "$PRD_FILE" 2>/dev/null)
        local completed=0
        if [[ -f "$PROGRESS_FILE" ]]; then
            completed=$(awk '/^## Tasks/{f=1;next} /^## /{f=0} f && /^\[x\]/' "$PROGRESS_FILE" 2>/dev/null | wc -l | tr -d ' ')
        fi

        # Guard against division by zero
        total=${total:-0}
        completed=${completed:-0}
        local percent=0
        if [[ "$total" -gt 0 ]]; then
            percent=$((completed * 100 / total))
        fi

        echo -e "${CYAN}┌─ PRD Status ────────────────────────────────────────────────────────────┐${NC}"
        echo -e "${CYAN}│${NC} Branch:         ${WHITE}$branch${NC}"
        echo -e "${CYAN}│${NC} Progress:       ${GREEN}$completed${NC}/${WHITE}$total${NC} tasks (${percent}%)"

        # Progress bar
        local bar_width=50
        local filled=0
        if [[ "$total" -gt 0 ]]; then
            filled=$((completed * bar_width / total))
        fi
        local empty=$((bar_width - filled))
        printf "${CYAN}│${NC} ["
        printf "${GREEN}%${filled}s" | tr ' ' '█'
        printf "${NC}%${empty}s" | tr ' ' '░'
        printf "] \n"

        echo -e "${CYAN}└─────────────────────────────────────────────────────────────────────────┘${NC}"
        echo

        # Next task — first '[ ]' line in progress.txt's '## Tasks' section
        local next_id="COMPLETE"
        local next_title="All done!"
        if [[ -f "$PROGRESS_FILE" ]]; then
            next_id=$(awk '/^## Tasks/{f=1;next} /^## /{f=0} f && /^\[ \]/{print $3; exit}' "$PROGRESS_FILE" 2>/dev/null)
            if [[ -n "$next_id" ]]; then
                next_title=$(jq -r --arg id "$next_id" '.tasks[] | select(.id==$id) | .title // "unknown"' "$PRD_FILE" 2>/dev/null)
            else
                next_id="COMPLETE"
            fi
        fi

        if [[ "$next_id" != "COMPLETE" ]]; then
            echo -e "${YELLOW}┌─ Next Task ─────────────────────────────────────────────────────────────┐${NC}"
            echo -e "${YELLOW}│${NC} ${WHITE}$next_id${NC}: $next_title"
            echo -e "${YELLOW}└─────────────────────────────────────────────────────────────────────────┘${NC}"
        else
            echo -e "${GREEN}┌─ STATUS ─────────────────────────────────────────────────────────────────┐${NC}"
            echo -e "${GREEN}│${NC} ALL STORIES COMPLETE!"
            echo -e "${GREEN}└─────────────────────────────────────────────────────────────────────────┘${NC}"
        fi
        echo

    else
        echo -e "${RED}┌─ Error ─────────────────────────────────────────────────────────────────┐${NC}"
        echo -e "${RED}│${NC} PRD file not found: $PRD_FILE"
        echo -e "${RED}└─────────────────────────────────────────────────────────────────────────┘${NC}"
        echo
    fi

    # Recent progress entries
    echo -e "${BLUE}┌─ Recent Progress ──────────────────────────────────────────────────────┐${NC}"
    if [[ -f "$PROGRESS_FILE" ]]; then
        # Show last 10 non-empty, non-separator lines
        grep -v '^---$' "$PROGRESS_FILE" | grep -v '^$' | tail -n 8 | while IFS= read -r line; do
            # Truncate long lines
            if [[ ${#line} -gt 72 ]]; then
                line="${line:0:69}..."
            fi
            echo -e "${BLUE}│${NC} $line"
        done
    else
        echo -e "${BLUE}│${NC} No progress file found"
    fi
    echo -e "${BLUE}└─────────────────────────────────────────────────────────────────────────┘${NC}"

    # Footer
    echo
    echo -e "${YELLOW}Refreshes every ${REFRESH_INTERVAL}s | $(date '+%Y-%m-%d %H:%M:%S') | Ctrl+C to exit${NC}"
}

# Main monitor loop
main() {
    echo "Starting Ralph Monitor..."
    sleep 1

    while true; do
        display_status
        sleep "$REFRESH_INTERVAL"
    done
}

main
