#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
# -*- coding: utf-8 -*-
"""Creates the NØR Browser source archive (release/NOR-Browser-source.zip).

Includes the full source tree plus smoke-test screenshots as evidence,
excluding build outputs, local caches and checkpoint archives.
"""
import os
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
RELEASE = os.path.join(ROOT, "release")
ARCHIVE = os.path.join(RELEASE, "NOR-Browser-source.zip")

EXCLUDE_DIRS = {"build", "checkpoints", "release", ".git", "__pycache__", "test-data"}
EXCLUDE_FILES = {".gitignore"}

# Smoke-test screenshots (evidence) copied from the build dir.
SCREENSHOTS = [
    ("build/smoke-navigation.png", "screenshots/smoke-navigation.png"),
    ("build/newtab-ar.png", "screenshots/newtab-ar.png"),
    ("build/newtab-fr.png", "screenshots/newtab-fr.png"),
]

# Real measured performance numbers (evidence).
EVIDENCE = [
    ("build/perf-measurements.txt", "perf-measurements.txt"),
    ("build/BASELINE-v1.0.0.txt", "baseline/BASELINE-v1.0.0.txt"),
]


def main() -> None:
    os.makedirs(RELEASE, exist_ok=True)
    if os.path.exists(ARCHIVE):
        os.remove(ARCHIVE)

    count = 0
    with zipfile.ZipFile(ARCHIVE, "w", zipfile.ZIP_DEFLATED) as zf:
        for dirpath, dirnames, filenames in os.walk(ROOT):
            dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS]
            for name in filenames:
                if name in EXCLUDE_FILES:
                    continue
                if name.endswith(".zip"):
                    continue
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, ROOT)
                zf.write(full, os.path.join("nor-browser", rel))
                count += 1
        for src, dst in SCREENSHOTS:
            full = os.path.join(ROOT, src)
            if os.path.exists(full):
                zf.write(full, os.path.join("nor-browser", dst))
                count += 1
        for src, dst in EVIDENCE:
            full = os.path.join(ROOT, src)
            if os.path.exists(full):
                zf.write(full, os.path.join("nor-browser", dst))
                count += 1

    size = os.path.getsize(ARCHIVE)
    print(f"wrote {ARCHIVE} ({count} files, {size/1024:.1f} KiB)")

    # verification pass
    with zipfile.ZipFile(ARCHIVE) as zf:
        names = zf.namelist()
        bad = zf.testzip()
        assert bad is None, f"corrupt member: {bad}"
        required = [
            "nor-browser/CMakeLists.txt",
            "nor-browser/src/main.cpp",
            "nor-browser/src/browser/BrowserWindow.cpp",
            "nor-browser/src/privacy/FilterRule.cpp",
            "nor-browser/src/privacy/RustAdblockBackend.cpp",
            "nor-browser/src/core/FilterListManager.cpp",
            "nor-browser/src/browser/AddressBarSuggestions.cpp",
            "nor-browser/adblock-sys/Cargo.toml",
            "nor-browser/adblock-sys/src/lib.rs",
            "nor-browser/adblock-sys/include/nor_adblock.h",
            "nor-browser/THIRD_PARTY_NOTICES.md",
            "nor-browser/docs/DEPENDENCIES.md",
            "nor-browser/resources/fonts/OFL.txt",
            "nor-browser/resources/backgrounds/README.md",
            "nor-browser/translations/nor-browser_ar.ts",
            "nor-browser/translations/nor-browser_fr.ts",
            "nor-browser/resources/filters/starter-blocklist.txt",
            "nor-browser/tests/fixtures/filterlists/easylist.txt",
            "nor-browser/tests/fixtures/filterlists/easyprivacy.txt",
            "nor-browser/tests/tst_adblock.cpp",
            "nor-browser/tests/tst_rustengine.cpp",
            "nor-browser/tests/tst_filterlists.cpp",
            "nor-browser/tests/tst_autocomplete.cpp",
            "nor-browser/tests/scripts/testserver.py",
            "nor-browser/docs/ADBLOCK_ENGINE.md",
            "nor-browser/docs/KNOWN_LIMITATIONS.md",
            "nor-browser/docs/WINDOWS_BUILD.md",
            "nor-browser/packaging/linux/nor-browser.desktop",
            "nor-browser/packaging/windows/nor-browser.rc",
            "nor-browser/LICENSE",
        ]
        missing = [r for r in required if r not in names]
        assert not missing, f"missing from archive: {missing}"
        print(f"verified: {len(names)} members, all {len(required)} required files present")


if __name__ == "__main__":
    main()
