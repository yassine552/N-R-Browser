#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
# -*- coding: utf-8 -*-
"""Fills the .ts translation files with Arabic and French translations.

Workflow (run from the project root, after lupdate):
    ../toolchain/Qt/6.8.2/gcc_64/bin/lupdate src/ -recursive \
        -ts translations/nor-browser_*.ts -no-obsolete
    python3 scripts/fill_translations.py            # this script
    (build regenerates .qm via lrelease / qt_add_translations)

English is the source language: its .ts stays untranslated on purpose
(English serves as the fallback for any missing string).
"""
import os
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
TRANSLATIONS = os.path.join(ROOT, "translations")

sys.path.insert(0, HERE)
from translations_ar import AR  # noqa: E402
from translations_fr import FR  # noqa: E402

LANGS = {
    "ar": AR,
    "fr": FR,
}


def fill(lang: str, catalog: dict) -> int:
    ts_path = os.path.join(TRANSLATIONS, f"nor-browser_{lang}.ts")
    tree = ET.parse(ts_path)
    root = tree.getroot()
    filled = 0
    missing = []

    for context in root.findall("context"):
        name = context.find("name").text
        catalog_ctx = catalog.get(name, {})
        for message in context.findall("message"):
            source = message.find("source").text
            translation = message.find("translation")
            if source in catalog_ctx:
                translation.text = catalog_ctx[source]
                if "type" in translation.attrib:
                    del translation.attrib["type"]
                translation.set("variant", "finished") if False else None
                # Mark as finished by removing the "unfinished" type attr.
                filled += 1
            else:
                missing.append(f"{name}: {source!r}")

    # Emit a clean file with the standard TS header.
    ET.indent(tree, space="    ")
    tree.write(ts_path, encoding="utf-8", xml_declaration=True)
    # Post-process: ensure exactly one DOCTYPE that Qt tools expect.
    with open(ts_path, "r", encoding="utf-8") as f:
        content = f.read()
    lines = [l for l in content.splitlines() if not l.startswith("<?xml") and not l.startswith("<!DOCTYPE")]
    body = "\n".join(lines)
    header = '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n'
    with open(ts_path, "w", encoding="utf-8") as f:
        f.write(header + body + "\n")

    print(f"[{lang}] filled {filled} translations into {os.path.basename(ts_path)}")
    if missing:
        print(f"[{lang}] MISSING {len(missing)} strings (will fall back to English):")
        for m in missing:
            print(f"        - {m}")
    return filled


def main() -> int:
    total = 0
    for lang, catalog in LANGS.items():
        total += fill(lang, catalog)
    print(f"total filled: {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
