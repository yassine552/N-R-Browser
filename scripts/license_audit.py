#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
"""Complete license-header audit for the NØR Browser source tree.

Verifies that
  1. every project source file carries the expected
     SPDX-License-Identifier (GPL-3.0-or-later for project code,
     MPL-2.0 for the adblock-sys shim),
  2. the adblock-sys manifest declares license = "MPL-2.0",
  3. no third-party or separately-licensed file carries a project GPL
     header (fonts OFL-1.1, backgrounds CC0-1.0, EasyList fixtures,
     translations/data/packaging assets, the LICENSE text itself),
  4. LICENSE is the GNU GPL v3 text.

Exit code 0 = clean, 1 = violations found. Run from anywhere:
  python3 scripts/license_audit.py
"""
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

GPL = "GPL-3.0-or-later"
MPL = "MPL-2.0"

# (glob pattern, expected SPDX id) — project code and data.
EXPECT = [
    ("src/**/*.cpp", GPL),
    ("src/**/*.h", GPL),
    ("src/**/CMakeLists.txt", GPL),
    ("tests/*.cpp", GPL),
    ("tests/*.h", GPL),
    ("tests/CMakeLists.txt", GPL),
    ("tests/scripts/*.py", GPL),
    ("tests/scripts/*.sh", GPL),
    ("scripts/*.py", GPL),
    ("scripts/*.sh", GPL),
    ("resources/html/*.html", GPL),
    ("resources/style/*", GPL),
    ("resources/filters/starter-blocklist.txt", GPL),
    ("packaging/linux/install.sh", GPL),
    ("packaging/windows/nor-browser.rc", GPL),
    ("CMakeLists.txt", GPL),
    ("adblock-sys/CMakeLists.txt", GPL),
    # MPL-2.0: original shim files, kept on the engine's license.
    ("adblock-sys/src/*.rs", MPL),
    ("adblock-sys/include/*.h", MPL),
]

# Files that must NOT carry any SPDX header: third-party material,
# dedicated/public-domain assets, data files covered by the project
# license, the license text itself, and documentation.
NO_HEADER = [
    "LICENSE",
    "README.md",
    "README.ar.md",
    "THIRD_PARTY_NOTICES.md",
    "docs/*.md",
    "resources/backgrounds/**",      # CC0 1.0 (see backgrounds/README.md)
    "resources/fonts/*",             # OFL-1.1 binaries + license text
    "resources/icons/*.svg",         # generated assets (project license)
    "translations/*.ts",
    "resources/resources.qrc",
    "tests/fixtures/**",             # upstream EasyList/EasyPrivacy data
    "packaging/linux/icons/**",
    "packaging/linux/nor-browser.desktop",
    "packaging/windows/nor-browser.ico",
    "adblock-sys/Cargo.toml",        # declares license = "MPL-2.0"
    "adblock-sys/Cargo.lock",
    "adblock-sys/rust-toolchain.toml",
    ".gitignore",
]

SPDX_RE = re.compile(r"SPDX-License-Identifier:\s*(\S[^\n]*?)\s*$",
                     re.MULTILINE)


def read(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def spdx_of(text):
    m = SPDX_RE.search(text)
    return m.group(1) if m else None


def main():
    violations = []
    notes = []

    # ---- 1. expected headers -------------------------------------------
    checked = 0
    for pattern, expected in EXPECT:
        for path in sorted(ROOT.glob(pattern)):
            if not path.is_file():
                continue
            checked += 1
            found = spdx_of(read(path))
            if expected is None:
                continue
            if found is None:
                violations.append(f"missing SPDX header: {path.relative_to(ROOT)}")
            elif found != expected:
                violations.append(
                    f"wrong SPDX '{found}' (expected {expected}): "
                    f"{path.relative_to(ROOT)}")

    # ---- 2. shim manifest ----------------------------------------------
    cargo = read(ROOT / "adblock-sys" / "Cargo.toml")
    if not re.search(r'^license\s*=\s*"MPL-2\.0"', cargo, re.MULTILINE):
        violations.append("adblock-sys/Cargo.toml does not declare "
                          'license = "MPL-2.0"')

    # ---- 3. no SPDX header on excepted files ---------------------------
    # Documentation legitimately *mentions* SPDX identifiers in prose, so
    # only a header in the leading lines counts as a real stamp.
    for pattern in NO_HEADER:
        for path in sorted(ROOT.glob(pattern)):
            if not path.is_file():
                continue
            head = "\n".join(read(path).splitlines()[:6])
            found = spdx_of(head)
            if found is not None:
                violations.append(
                    f"unexpected SPDX '{found}' on excepted file: "
                    f"{path.relative_to(ROOT)}")

    # ---- 4. LICENSE is the GPLv3 text ----------------------------------
    lic = read(ROOT / "LICENSE")
    for marker in ("GNU GENERAL PUBLIC LICENSE",
                   "Version 3, 29 June 2007",
                   "END OF TERMS AND CONDITIONS"):
        if marker not in lic:
            violations.append(f"LICENSE does not look like the GPL-3.0 text "
                              f"(missing: {marker!r})")
    notes.append(f"LICENSE: {len(lic.encode('utf-8'))} bytes, "
                 f"sha256 {hashlib.sha256(lic.encode('utf-8')).hexdigest()[:16]}…")

    # ---- report ---------------------------------------------------------
    print(f"license audit: {checked} source files checked, "
          f"{len(EXPECT)} rules, {len(NO_HEADER)} exception classes")
    for note in notes:
        print(f"  · {note}")
    if violations:
        print(f"\n{len(violations)} VIOLATIONS:")
        for v in violations:
            print(f"  ✗ {v}")
        return 1
    print("\nOK — all project sources carry the expected SPDX headers; "
          "no third-party or separately licensed file is mis-stamped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
