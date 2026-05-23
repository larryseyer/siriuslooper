#!/usr/bin/env bash
# =============================================================================
# bash/test-s7.sh — automated M7 S7 end-to-end lifecycle test
# =============================================================================
# Runs the three [main-component-plugin-editor] headless lifecycle tests
# against the synthetic CLAP plug-in fixture. Exercises the full
# MainComponent → OutOfProcessEffectChainHost → ida_plugin_host →
# CARemoteLayer chain WITHOUT a GUI.
#
# Pass result means S7's IPC + supervisor + window lifecycle works
# correctly. The only thing this can't verify is cross-process pixel
# compositing (CI is headless); that needs the .app eyes-on procedure.
#
# Usage: bash bash/test-s7.sh
# =============================================================================

set -euo pipefail

cd "$(dirname "$0")/.."

HOST="$(pwd)/build/host_process/ida_plugin_host"
CLAP="$(pwd)/build/tests/fixtures/SyntheticTestPlugin.clap"
TEST="$(pwd)/build/tests/MainComponentPluginEditorTests"

if [[ ! -x "$TEST" ]]; then
    echo "test binary missing — building first"
    cmake --build build -j --target MainComponentPluginEditorTests >/dev/null
fi

if [[ ! -x "$HOST" ]]; then
    echo "host binary missing — building first"
    cmake --build build -j --target ida_plugin_host >/dev/null
fi

if [[ ! -d "$CLAP" ]]; then
    echo "synthetic CLAP fixture missing — building first"
    cmake --build build -j --target SyntheticTestPlugin >/dev/null
fi

# hostBinaryPath() resolves from the test binary's parent dir; symlink
# the helper so the test's SKIP-on-missing-fixture guard doesn't fire.
ln -sfn "$HOST" "$(dirname "$TEST")/ida_plugin_host"

IDA_PLUGIN_HOST_PATH="$HOST" \
IDA_SYNTHETIC_CLAP_PATH="$CLAP" \
    "$TEST" --reporter console
