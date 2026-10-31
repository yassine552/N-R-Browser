#!/usr/bin/env bash
# NØR Browser — performance measurements (real runs, no estimates).
# Measures: startup time to first page load, idle RSS (whole process tree)
# with 1/5 tabs, with the full EasyList+EasyPrivacy engine loaded.
# Usage: scripts/measure_perf.sh [build-dir]
set -u
BUILD_DIR="${1:-$(dirname "$0")/../build}"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
# Optional locally-bundled Qt toolchain (container builds): set
# NOR_TOOLCHAIN to the toolchain root (containing syslibs/) when Qt's
# runtime libraries are not system-installed; leave unset for a normal
# system Qt installation.
TOOLCHAIN="${NOR_TOOLCHAIN:-}"
PORT=8397
DISPLAY_NUM=:98
OUT="$BUILD_DIR/perf-measurements.txt"

# A representative page set: the local test server pages plus a real page.
tree_rss_kb() {  # sum RSS of the browser process and its children, in kB
    local pid=$1 total=0
    for p in $pid $(pgrep -P $pid 2>/dev/null); do
        local rss
        rss=$(awk '/VmRSS/{print $2}' "/proc/$p/status" 2>/dev/null)
        total=$((total + ${rss:-0}))
    done
    echo $total
}

pkill -f "testserver.py $PORT" 2>/dev/null || true
pkill -f "Xvfb $DISPLAY_NUM" 2>/dev/null || true
sleep 1
python3 "$PROJECT/tests/scripts/testserver.py" $PORT > /tmp/nor-perf-server.log 2>&1 &
SERVER_PID=$!
Xvfb $DISPLAY_NUM -screen 0 1366x768x24 -nolisten tcp > /dev/null 2>&1 &
XVFB_PID=$!
trap 'kill $SERVER_PID $XVFB_PID 2>/dev/null; true' EXIT
sleep 2

export DISPLAY=$DISPLAY_NUM
if [ -n "$TOOLCHAIN" ] && [ -d "$TOOLCHAIN/syslibs" ]; then
    export LD_LIBRARY_PATH="$TOOLCHAIN/syslibs/usr/lib/x86_64-linux-gnu"
fi
export QTWEBENGINE_DISABLE_SANDBOX=1
export QTWEBENGINE_CHROMIUM_FLAGS="--disable-dev-shm-usage --disable-gpu"
export XDG_DATA_HOME="$BUILD_DIR/perf-data/data"
export XDG_CACHE_HOME="$BUILD_DIR/perf-data/cache"
export XDG_CONFIG_HOME="$BUILD_DIR/perf-data/config"
rm -rf "$BUILD_DIR/perf-data"
mkdir -p "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME"

{
echo "NOR Browser performance measurements — $(date -u '+%Y-%m-%d %H:%M UTC')"
echo "machine: $(nproc) cores, $(free -m | awk '/^Mem:/{print $2}') MB RAM, software rendering (Xvfb)"
echo

# --- startup: cold (first run, engine cache created) -----------------------
# The smoke mode exits right after the page finishes loading, so the wall
# time below = engine init + first page load + teardown.
T0=$(date +%s%3N)
"$BUILD_DIR/nor-browser" --smoke-https "https://example.com" >/dev/null 2>&1
T1=$(date +%s%3N)
echo "startup+load, first run (smoke exit):  $((T1 - T0)) ms"

# --- startup: warm (engine cache present) ----------------------------------
T0=$(date +%s%3N)
"$BUILD_DIR/nor-browser" --smoke-https "https://example.com" >/dev/null 2>&1
T1=$(date +%s%3N)
echo "startup+load, engine cache warm:        $((T1 - T0)) ms"
echo

# --- idle memory with tabs --------------------------------------------------
# Open the browser with real pages and sample the process tree RSS.
"$BUILD_DIR/nor-browser" "http://127.0.0.1:$PORT/hello" >/dev/null 2>&1 &
BROWSER_PID=$!
sleep 8   # settle: WebEngine spawns render/GPU processes
RSS1=$(tree_rss_kb $BROWSER_PID)
echo "idle RSS, 1 tab (whole process tree):   $((RSS1 / 1024)) MB ($RSS1 kB)"

# Keep the first window open and add 4 more (approximation of several tabs).
EXTRA_PIDS=""
for i in 1 2 3 4; do
    "$BUILD_DIR/nor-browser" "http://127.0.0.1:$PORT/links" >/dev/null 2>&1 &
    EXTRA_PIDS="$EXTRA_PIDS $!"
done
sleep 10
TOTAL=0
for p in $BROWSER_PID $EXTRA_PIDS; do
    [ -d "/proc/$p" ] || continue
    TOTAL=$((TOTAL + $(tree_rss_kb $p)))
done
kill $BROWSER_PID $EXTRA_PIDS 2>/dev/null
echo "RSS, 5 windows/6 pages total:           $((TOTAL / 1024)) MB ($TOTAL kB)"
sleep 2

# --- engine cache size ------------------------------------------------------
CACHE="$XDG_DATA_HOME/NOR/nor-browser/filterlists/engine.cache"
if [ -f "$CACHE" ]; then
    echo
    echo "engine cache size:                      $(du -h "$CACHE" | cut -f1)"
fi
LISTS=$(ls "$XDG_DATA_HOME/NOR/nor-browser/filterlists/"*.txt 2>/dev/null | wc -l)
echo "cached filter lists downloaded:          $LISTS"
} | tee "$OUT"

echo
echo "measurements saved to $OUT"
