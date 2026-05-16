#!/bin/bash
# Convenience wrapper - calls bash/bu.sh
exec "$(dirname "$0")/bash/bu.sh" "$@"
