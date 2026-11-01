#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
"""Assembles THIRD_PARTY_NOTICES.md from the resolved Cargo.lock
dependency set, embedding the full license texts found in the local
cargo registry cache. Run from nor-browser/ after building adblock-sys
(so the registry cache is populated).
"""
import glob
import os
import re

CARGO_HOME = os.environ.get(
    "CARGO_HOME", os.path.expanduser("~/.cargo"))
REG = glob.glob(os.path.join(CARGO_HOME, "registry", "src", "*"))[0]
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))

SHIM = "nor-adblock-sys"

# Where each embedded license text is taken from (crate, file).
TEXT_SOURCE = {
    "MPL-2.0": ("adblock", "0.13.5", "LICENSE"),
    "MIT": ("idna_adapter", "1.2.2", "LICENSE-MIT"),
    "Apache-2.0": ("idna_adapter", "1.2.2", "LICENSE-APACHE"),
    "Unlicense": ("aho-corasick", "1.1.5", "UNLICENSE"),
    "Unicode-3.0": ("icu_collections", "2.4.0", "LICENSE"),
}

# Raw Cargo license expression -> set of license texts that apply.
LICENSE_MAP = {
    "MPL-2.0": {"MPL-2.0"},
    "MIT": {"MIT"},
    "Apache-2.0": {"Apache-2.0"},
    "MIT/Apache-2.0": {"MIT", "Apache-2.0"},
    "MIT OR Apache-2.0": {"MIT", "Apache-2.0"},
    "Apache-2.0 OR MIT": {"MIT", "Apache-2.0"},
    "(MIT OR Apache-2.0) AND Unicode-3.0": {"MIT", "Apache-2.0", "Unicode-3.0"},
    "Unlicense OR MIT": {"Unlicense", "MIT"},
    "Unicode-3.0": {"Unicode-3.0"},
}


def crate_file(crate, version, name):
    p = os.path.join(REG, f"{crate}-{version}", name)
    return p if os.path.exists(p) else None


def license_of(name, version):
    if name == SHIM:
        return "MPL-2.0 (NOR Browser shim)"
    d = os.path.join(REG, f"{name}-{version}")
    mf = os.path.join(d, "Cargo.toml")
    if not os.path.exists(mf):
        return "not-found"
    text = open(mf, encoding="utf-8", errors="replace").read()
    m = re.search(r'license\s*=\s*"([^"]+)"', text)
    return m.group(1) if m else "not-declared"


def collect():
    lock = open(os.path.join(ROOT, "adblock-sys", "Cargo.lock")).read()
    pkgs = re.findall(
        r'\[\[package\]\]\nname = "([^"]+)"\nversion = "([^"]+)"', lock)
    rows = []
    for name, version in pkgs:
        rows.append((name, version, license_of(name, version)))
    return sorted(rows)


INTRO = """\
NØR Browser itself is free software licensed under the **GNU General
Public License v3.0 or later** (`GPL-3.0-or-later`, see `LICENSE`).

The `adblock-sys` Rust C-ABI shim (`adblock-sys/Cargo.toml`,
`adblock-sys/src/lib.rs`, `adblock-sys/include/nor_adblock.h`) is
original NØR Browser code deliberately kept under **MPL-2.0**, like the
MPL-2.0 `adblock-rust` engine it wraps. MPL-2.0 §3.3 ("Distribution of
a Larger Work") permits those files to be compiled into and distributed
as part of this larger GPL-3.0-or-later application, **while they
remain under MPL-2.0** and keep their MPL obligations: their full
source is shipped in this source archive (§3.1/§3.2) and their license
notices are preserved (§3.4). This is *not* a secondary GPL license:
GPL-3.0 is not among the Secondary Licenses defined in MPL-2.0 §1.12
(those are GPLv2, LGPLv2.1 and AGPLv3), so no such grant is claimed or
needed — the Larger Work provision of §3.3 is what applies.

Qt is not redistributed in this archive and is linked dynamically at
runtime. Qt Core/Gui/Widgets/Network/Sql/Test are offered by The Qt
Company under LGPL-3.0-only OR GPL-3.0-only OR GPL-2.0-only OR
commercial terms; Qt WebEngine's Qt-specific parts under LGPL-3.0 OR
GPL-3.0 OR GPL-2.0 OR commercial, while its embedded Chromium/Blink
portion carries its own separate third-party licenses (predominantly
BSD-3-Clause, the most restrictive being LGPL-2.1, plus ICU under the
ICU license). See `docs/DEPENDENCIES.md` for the per-module breakdown
and binary-distribution obligations.
"""

ASSETS = """\
| Asset | Source | License |
|---|---|---|
| Noto Sans Arabic (Regular) | Noto project — https://github.com/notofonts (hinted/ttf/NotoSansArabic) | SIL Open Font License 1.1 — full text in `resources/fonts/OFL.txt` |
| Noto Naskh Arabic (Regular) | Noto project — https://github.com/notofonts (hinted/ttf/NotoNaskhArabic) | SIL Open Font License 1.1 — full text in `resources/fonts/OFL.txt` |
| New-tab backgrounds (`resources/backgrounds/nor-*.jpg` + thumbnails) | original artworks, procedurally generated for this project (no third-party sources) | CC0 1.0 / public domain — see `resources/backgrounds/README.md` |

Original NØR Browser assets (SVG icons, app icons, the starter
blocklist, translations) are part of the GPL-3.0-or-later work and are
not listed here as third-party material — see `docs/DEPENDENCIES.md`.

The EasyList and EasyPrivacy filter lists are NOT compiled into the
binary: they are downloaded on the user's machine when automatic
updates are enabled (or skipped entirely offline) and remain under
their authors' terms.
"""

ATTRIBUTION = [
    "**brave/adblock-rust** (https://github.com/brave/adblock-rust) — "
    "the ad-blocking engine. Copyright (c) the Brave adblock-rust "
    "authors. MPL-2.0. The NØR Browser integration does not modify the "
    "crate; it is consumed as published on crates.io, pinned to exactly "
    "0.13.5 in `adblock-sys/Cargo.toml`.",
    "**Public Suffix List** — embedded inside the `addr` crate "
    "(registry-suffix data). https://publicsuffix.org — see the "
    "`addr`/`psl` crate license terms above.",
    "**Qt 6 / Qt WebEngine** (Copyright (c) The Qt Company Ltd and "
    "contributors) — linked dynamically, NOT redistributed inside the "
    "NØR source archive; per-module license options are documented in "
    "`docs/DEPENDENCIES.md`. Qt WebEngine additionally embeds Chromium "
    "and other third-party code under their own licenses (BSD-3-Clause "
    "and others — most restrictive LGPL-2.1); those notices travel with "
    "the Qt WebEngine packages you obtain, not with this archive.",
    "**EasyList / EasyPrivacy** (https://easylist.to) — downloaded at "
    "runtime under their own terms. The EasyList repository contents "
    "are dual-licensed GPL-3.0-or-later OR CC-BY-SA-3.0-or-later, with "
    "attribution to \"The EasyList authors (https://easylist.to/)\"; "
    "the easylist.to licence page notes that externally-hosted files "
    "(including EasyPrivacy) may carry their own conditions. Offline "
    "test snapshots are redistributed inside this archive as "
    "`tests/fixtures/filterlists/` for the automated test suite, under "
    "those same upstream terms.",
]


def main():
    rows = collect()

    # license-text key -> crates it applies to (through any expression)
    applies = {}
    unknown = []
    for name, version, lic in rows:
        if name == SHIM:
            continue  # handled in its own section
        texts = LICENSE_MAP.get(lic)
        if texts is None:
            unknown.append((name, version, lic))
            continue
        for t in texts:
            applies.setdefault(t, []).append(f"{name} {version}")

    out = []
    w = out.append
    w("# Third-Party Notices for NØR Browser\n")
    w("This file lists the third-party open-source components that are "
      "compiled into NØR Browser when the adblock-rust backend is "
      "enabled (`NOR_ADBLOCK_BACKEND=Rust`, the default when a Rust "
      "toolchain is available), together with their licenses. Versions "
      "are the exact versions resolved by Cargo and recorded in "
      "`adblock-sys/Cargo.lock`. Regenerate with "
      "`scripts/gen_third_party_notices.py`.\n")
    w(INTRO)
    w("## Bundled assets\n")
    w(ASSETS)
    w("## Component summary\n")
    w("| Component | Version | License (as declared in its manifest) |")
    w("|---|---|---|")
    for name, version, lic in rows:
        w(f"| {name} | {version} | {lic} |")
    w("")
    w("All of the above third-party licenses are permissive or "
      "weak-copyleft and compatible with the distribution of NØR "
      "Browser as described above. Where a dependency offers a choice "
      "(\"MIT OR Apache-2.0\", \"Unlicense OR MIT\"), all options of "
      "that choice are reproduced below; the crates are consumed as "
      "published.\n")
    w("## License texts\n")
    w("Each text below is the standard form of that license. Every "
      "crate listed under it carries **its own copyright holders** — "
      "the copyright notices for each crate are contained in its own "
      "LICENSE file in the upstream distribution and in the local "
      "cargo registry cache; they are not merged into this document.\n")

    order = ["MPL-2.0", "MIT", "Apache-2.0", "Unlicense", "Unicode-3.0"]
    for key in order:
        crates = applies.get(key)
        if not crates:
            continue
        w(f"### {key}\n")
        w(f"Applies to: {', '.join(crates)}\n")
        src = TEXT_SOURCE[key]
        path = crate_file(*src)
        if path is None:
            w("(License text not present in the local registry cache; "
              "see the upstream repository.)\n")
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        w("```")
        w(text.rstrip())
        w("```\n")

    w(f"### MPL-2.0 — the NØR Browser shim (`{SHIM}`)\n")
    w(f"Applies to: {SHIM} 1.0.0\n")
    w("The C-ABI shim between the GPL-3.0-or-later C++ application and "
      "the MPL-2.0 `adblock-rust` engine is original NØR Browser code, "
      "deliberately licensed under MPL-2.0 (`adblock-sys/Cargo.toml`, "
      "`license = \"MPL-2.0\"`). Its source files — "
      "`adblock-sys/src/lib.rs` and `adblock-sys/include/nor_adblock.h` "
      "— carry MPL-2.0 SPDX identifiers and the Exhibit A notice, are "
      "distributed in full in this source archive, and are compiled "
      "into the larger GPL-3.0-or-later work under MPL-2.0 §3.3 as "
      "explained in the introduction. The MPL-2.0 full text above "
      "applies to them as well.\n")

    for name, version, lic in unknown:
        w(f"> Note: license `{lic}` of `{name} {version}` is not "
          "recognized by this generator; check the crate manifest "
          "manually.\n")

    w("## Attribution notes\n")
    for note in ATTRIBUTION:
        w(f"- {note}")
    w("")

    path = os.path.join(ROOT, "THIRD_PARTY_NOTICES.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {path} ({len(rows)} components, "
          f"{len(applies)} license texts)")


if __name__ == "__main__":
    main()
