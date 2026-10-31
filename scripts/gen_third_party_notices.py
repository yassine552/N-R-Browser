#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Assembles THIRD_PARTY_NOTICES.md from the resolved Cargo.lock
dependency set, embedding the full license texts found in the local
cargo registry cache. Run from nor-browser/ after building adblock-sys.
"""
import glob
import os
import re

CARGO_HOME = os.environ.get(
    "CARGO_HOME", os.path.expanduser("~/.cargo"))
REG = glob.glob(os.path.join(CARGO_HOME, "registry", "src", "*"))[0]
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))


def crate_license_text(crate_dir):
    for name in ("LICENSE", "LICENSE.txt", "LICENSE-MIT", "COPYING"):
        p = os.path.join(crate_dir, name)
        if os.path.exists(p):
            return open(p, encoding="utf-8", errors="replace").read()
    return None


def license_of(name, version):
    d = os.path.join(REG, f"{name}-{version}")
    if not os.path.isdir(d):
        return "not-found"
    mf = os.path.join(d, "Cargo.toml")
    if not os.path.exists(mf):
        return "no-manifest"
    text = open(mf, encoding="utf-8", errors="replace").read()
    m = re.search(r'license\s*=\s*"([^"]+)"', text)
    return m.group(1) if m else "not-declared"


def collect():
    lock = open(os.path.join(ROOT, "adblock-sys", "Cargo.lock")).read()
    pkgs = re.findall(
        r'\[\[package\]\]\nname = "([^"]+)"\nversion = "([^"]+)"', lock)
    rows = []
    for name, version in pkgs:
        lic = "MPL-2.0 (NOR Browser shim)" if name == "nor-adblock-sys" \
            else license_of(name, version)
        rows.append((name, version, lic))
    return sorted(rows)


def group(rows):
    groups = {}
    for name, version, lic in rows:
        key = lic.replace(" OR ", "/").replace("(MIT/Apache-2.0)", "MIT/Apache-2.0")
        groups.setdefault(key, []).append(f"{name} {version}")
    return groups


REPRESENTATIVE = {
    "MPL-2.0": ("adblock", "0.13.5"),
    "MPL-2.0 (NOR Browser shim)": None,
    "MIT/Apache-2.0": ("addr", "0.15.6"),
    "MIT OR Apache-2.0": ("itertools", "0.14.0"),
    "Apache-2.0 OR MIT": ("idna_adapter", "1.2.2"),
    "Apache-2.0": ("flatbuffers", "25.12.19"),
    "MIT": ("seahash", "4.1.0"),
    "Unicode-3.0": ("icu_collections", "2.4.0"),
    "(MIT OR Apache-2.0) AND Unicode-3.0": ("unicode-ident", "1.0.27"),
    "Unlicense OR MIT": ("memchr", "2.8.3"),
}


def main():
    rows = collect()
    groups = group(rows)
    out = []
    w = out.append
    w("# Third-Party Notices for NØR Browser\n")
    w("This file lists the third-party open-source components that are "
      "compiled into NØR Browser when the adblock-rust backend is enabled "
      "(`NOR_ADBLOCK_BACKEND=Rust`, the default when a Rust toolchain is "
      "available), together with their licenses. Versions are the exact "
      "versions resolved by Cargo and recorded in "
      "`adblock-sys/Cargo.lock`.\n")
    w("NØR Browser itself is licensed under the MIT license (see "
      "`LICENSE`).\n")
    w("## Component summary\n")
    w("| Component | Version | License |")
    w("|---|---|---|")
    for name, version, lic in rows:
        w(f"| {name} | {version} | {lic} |")
    w("")
    w("All of the above licenses are permissive and compatible with the "
      "distribution of NØR Browser. Where a dependency offers a choice "
      "(\"MIT OR Apache-2.0\", \"Unlicense OR MIT\"), the MIT/Apache-2.0 "
      "terms apply as the combination actually compiled in.\n")
    w("## License texts\n")
    seen_texts = set()
    for lic_key, crates in sorted(groups.items()):
        rep = REPRESENTATIVE.get(lic_key, None)
        w(f"### {lic_key}\n")
        w(f"Applies to: {', '.join(crates)}\n")
        if rep is None:
            w("The NØR Browser C-ABI shim itself; see `LICENSE` for the "
              "project license and `adblock-sys/src/lib.rs` for the "
              "copyright notice required by MPL-2.0 §3.1 (see below).\n")
            continue
        text = crate_license_text(os.path.join(REG, f"{rep[0]}-{rep[1]}"))
        if text is None or text in seen_texts:
            if text is None:
                w("(License text not present in the local registry cache; "
                  "see the upstream repository.)\n")
            continue
        seen_texts.add(text)
        w("```")
        w(text.rstrip())
        w("```\n")
    w("## Attribution notes\n")
    w("- **brave/adblock-rust** (https://github.com/brave/adblock-rust) — "
      "the ad-blocking engine. Copyright (c) the Brave adblock-rust "
      "authors. MPL-2.0. The NØR Browser integration does not modify the "
      "crate; it is consumed as published on crates.io, pinned to exactly "
      "0.13.5 in `adblock-sys/Cargo.toml`.")
    w("- **Public Suffix List** — embedded inside the `addr` crate "
      "(registry-suffix data). https://publicsuffix.org — see the "
      "`addr`/`psl` crate license terms above.")
    w("- **Qt 6 / Qt WebEngine** (The Qt Company, LGPL-3.0 / GPL-3.0 as "
      "applicable) is linked dynamically and is NOT redistributed inside "
      "the NØR source archive; see `docs/DEPENDENCIES.md` for build "
      "requirements.")
    w("- Filter lists (EasyList, EasyPrivacy) are downloaded at runtime "
      "from https://easylist.to under their own terms; they are cached in "
      "the user's profile directory and are not redistributed inside this "
      "archive except as offline test fixtures under "
      "`tests/fixtures/filterlists/` for the automated test suite.")

    path = os.path.join(ROOT, "THIRD_PARTY_NOTICES.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {path} ({len(rows)} components)")


if __name__ == "__main__":
    main()
