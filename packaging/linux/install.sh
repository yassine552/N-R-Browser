#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
#
# Installs NØR Browser for the current user (no root required).
# Usage: packaging/linux/install.sh [build-dir]
set -eu

BUILD_DIR="${1:-$(cd "$(dirname "$0")/../.." && pwd)/build}"
PREFIX="${HOME}/.local"

if [ ! -x "$BUILD_DIR/nor-browser" ]; then
    echo "nor-browser not found in $BUILD_DIR — build it first." >&2
    exit 1
fi

mkdir -p "$PREFIX/bin" "$PREFIX/share/applications" "$PREFIX/share/icons/hicolor"

install -m 755 "$BUILD_DIR/nor-browser" "$PREFIX/bin/nor-browser"
install -m 644 "$(dirname "$0")/nor-browser.desktop" \
        "$PREFIX/share/applications/nor-browser.desktop"

for size in 16 22 24 32 48 64 128 256; do
    src="$(dirname "$0")/icons/hicolor/${size}x${size}/apps/nor-browser.png"
    dst="$PREFIX/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$dst"
    install -m 644 "$src" "$dst/nor-browser.png"
done

# Compliance: ship the license texts next to the installed binary so
# recipients of the Executable Form get the GPL (project), the third-party
# notices (MPL shim + Rust crates + Qt/Chromium notes) and the OFL font
# license (MPL-2.0 §3.2, LGPL-3.0 §4(d)/§6, OFL 1.1 redistribution terms).
SRC_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DOC_DIR="$PREFIX/share/doc/nor-browser"
mkdir -p "$DOC_DIR"
install -m 644 "$SRC_ROOT/LICENSE" "$DOC_DIR/LICENSE"
install -m 644 "$SRC_ROOT/THIRD_PARTY_NOTICES.md" \
        "$DOC_DIR/THIRD_PARTY_NOTICES.md"
install -m 644 "$SRC_ROOT/resources/fonts/OFL.txt" \
        "$DOC_DIR/OFL.txt"

update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
update-mime-database "$PREFIX/share/mime" 2>/dev/null || true

echo "Installed to $PREFIX/bin/nor-browser"
echo "Note: Qt 6.8 runtime libraries must be available (e.g. installed"
echo "system-wide, or in LD_LIBRARY_PATH) for the browser to start."
