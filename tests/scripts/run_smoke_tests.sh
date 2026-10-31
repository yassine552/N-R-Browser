#!/usr/bin/env bash
# NØR Browser — end-to-end smoke test runner.
#
# Starts the local test server and a virtual X display, then runs every
# browser smoke mode against them. Exits non-zero if any mode fails.
#
# Usage: tests/scripts/run_smoke_tests.sh [build-dir]
set -u

BUILD_DIR="${1:-$(dirname "$0")/../../build}"
# XDG spec requires absolute paths; relative values are ignored.
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
PROJECT="$(cd "$(dirname "$0")/../.." && pwd)"
# Optional locally-bundled Qt toolchain (container builds). Set
# NOR_TOOLCHAIN to the toolchain root (containing syslibs/) when Qt's
# runtime libraries are not installed system-wide; otherwise leave it
# unset for a normal system Qt.
TOOLCHAIN="${NOR_TOOLCHAIN:-}"
PORT=8377
DISPLAY_NUM=:99

if [ ! -x "$BUILD_DIR/nor-browser" ]; then
    echo "nor-browser executable not found in $BUILD_DIR" >&2
    echo "Build it first (see README.md)." >&2
    exit 2
fi

# Clean environment from previous runs.
pkill -f "testserver.py" 2>/dev/null || true
pkill -f "Xvfb $DISPLAY_NUM" 2>/dev/null || true
sleep 1

python3 "$PROJECT/tests/scripts/testserver.py" $PORT > "$BUILD_DIR/testserver.log" 2>&1 &
SERVER_PID=$!
Xvfb $DISPLAY_NUM -screen 0 1366x768x24 -nolisten tcp > "$BUILD_DIR/xvfb.log" 2>&1 &
XVFB_PID=$!
trap 'kill $SERVER_PID $XVFB_PID 2>/dev/null; true' EXIT
sleep 2

if ! curl -s -o /dev/null "http://127.0.0.1:$PORT/hello"; then
    echo "test server failed to start" >&2
    exit 2
fi

export DISPLAY=$DISPLAY_NUM
# Only inject the bundled-toolchain library path when NOR_TOOLCHAIN is set.
if [ -n "$TOOLCHAIN" ] && [ -d "$TOOLCHAIN/syslibs" ]; then
    export LD_LIBRARY_PATH="$TOOLCHAIN/syslibs/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
# The Chromium sandbox cannot start inside this container (no user
# namespaces); this affects only the TEST environment, never the shipped
# browser configuration.
export QTWEBENGINE_DISABLE_SANDBOX=1
# TEST-ENVIRONMENT-ONLY flags: Chromium (122) permanently rejects
# "SameSite=None" cookies without Secure, which would ALSO block the
# cross-site test cookie and hide whether NØR's own cookie filter works.
# Treating the loopback test origin as a secure context lets the cookie
# through Chromium, so only the browser's filter decides its fate.
# The shipped browser never sets these flags.
export QTWEBENGINE_CHROMIUM_FLAGS="--disable-dev-shm-usage --disable-gpu --unsafely-treat-insecure-origin-as-secure=http://127.0.0.1:$PORT"
export XDG_DATA_HOME="$BUILD_DIR/test-data/data"
export XDG_CACHE_HOME="$BUILD_DIR/test-data/cache"
export XDG_CONFIG_HOME="$BUILD_DIR/test-data/config"

fails=0

run_mode() {
    # run_mode <name> <browser args...>
    # Optional extra Chromium flags for THIS mode only: set NOR_MODE_FLAGS
    # before calling (used by run_mode_extra).
    local name="$1"; shift
    echo "===== $name ====="
    local out
    local saved_flags="${QTWEBENGINE_CHROMIUM_FLAGS:-}"
    if [ -n "${NOR_MODE_FLAGS:-}" ]; then
        export QTWEBENGINE_CHROMIUM_FLAGS="$saved_flags $NOR_MODE_FLAGS"
    fi
    # Capture the browser's raw output AND its true exit code: piping through
    # grep and `|| true` would hide crashes (PIPESTATUS would belong to the
    # `true` command, so a browser that never started counted as "passed").
    out=$(timeout 120 "$BUILD_DIR/nor-browser" "$@" 2>&1)
    local rc=$?
    if [ -n "${NOR_MODE_FLAGS:-}" ]; then
        export QTWEBENGINE_CHROMIUM_FLAGS="$saved_flags"
    fi
    printf '%s\n' "$out" | grep -E "SMOKE|QWARN" || true
    # A mode that produced no SMOKE RESULT line crashed before finishing.
    if [ "$rc" -ne 0 ]; then
        echo ">>> $name FAILED (exit $rc)"
        fails=$((fails + 1))
    elif ! printf '%s\n' "$out" | grep -q "SMOKE RESULT"; then
        echo ">>> $name FAILED (no SMOKE RESULT output; browser crashed?)"
        printf '%s\n' "$out" | tail -5
        fails=$((fails + 1))
    fi
}

run_mode_extra() {
    # run_mode_extra <name> <extra-chromium-flags> <browser args...>
    local name="$1"; local flags="$2"; shift 2
    NOR_MODE_FLAGS="$flags" run_mode "$name" "$@"
}

run_mode "navigation" --smoke "http://127.0.0.1:$PORT" \
    --screenshot "$BUILD_DIR/smoke-navigation.png"
run_mode "private-browsing" --smoke-private "http://127.0.0.1:$PORT"
run_mode "language-switching" --smoke-language "$BUILD_DIR"
run_mode "downloads" --smoke-download "http://127.0.0.1:$PORT"
run_mode "cosmetic-filtering" --smoke-cosmetic "http://127.0.0.1:$PORT"
run_mode "network-blocking" --smoke-blocking "http://127.0.0.1:$PORT" \
    --smoke-easylist "$PROJECT/tests/fixtures/filterlists/easylist.txt" \
    --smoke-easyprivacy "$PROJECT/tests/fixtures/filterlists/easyprivacy.txt"
run_mode_extra "privacy-hardening" "--ignore-certificate-errors" \
    --smoke-privacy "http://127.0.0.1:$PORT"
run_mode "tls-reject-self-signed" --smoke-tls-bad "https://127.0.0.1:$((PORT + 1))/hello"
run_mode "https-real-internet" --smoke-https "https://example.com"

# Top Sites need a PRISTINE history for the new-user phase: run against a
# dedicated scratch home (wiped each run) instead of the shared test-data.
TOPSITES_SCRATCH="$BUILD_DIR/test-data-topsites"
rm -rf "$TOPSITES_SCRATCH"
mkdir -p "$TOPSITES_SCRATCH/data" "$TOPSITES_SCRATCH/cache" "$TOPSITES_SCRATCH/config"
SAVED_DATA_HOME="$XDG_DATA_HOME"; SAVED_CACHE_HOME="$XDG_CACHE_HOME"; SAVED_CONFIG_HOME="$XDG_CONFIG_HOME"
export XDG_DATA_HOME="$TOPSITES_SCRATCH/data"
export XDG_CACHE_HOME="$TOPSITES_SCRATCH/cache"
export XDG_CONFIG_HOME="$TOPSITES_SCRATCH/config"
run_mode "top-sites" --smoke-topsites "http://127.0.0.1:$PORT" \
    --screenshot "$BUILD_DIR/topsites-shots"
export XDG_DATA_HOME="$SAVED_DATA_HOME" XDG_CACHE_HOME="$SAVED_CACHE_HOME" XDG_CONFIG_HOME="$SAVED_CONFIG_HOME"

echo "===== SMOKE SUITE SUMMARY: $fails failed mode(s) ====="
exit $fails
