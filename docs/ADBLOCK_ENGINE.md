# The NØR Ad-Block Engine

This document records the engine selection decision, the integration
architecture and the measured results.

## 1. Selection

Candidates evaluated for network-request + cosmetic filtering:

| Candidate | Language | License | Verdict |
|---|---|---|---|
| **brave/adblock-rust 0.13.5** | Rust | MPL-2.0 | **Selected.** Full EasyList/EasyPrivacy grammar, cosmetic filtering, engine serialization, embedded Public Suffix List, actively maintained, pinned exactly via crates.io. |
| adblockplus/libadblockplus | C++ (requires full V8) | GPL-3.0 | Rejected: linking a second, complete V8 alongside Qt WebEngine's own is far too heavy for the 8 GB target (build, RAM and maintenance). |
| uBlock Origin | JavaScript (WebExtension) | GPL-3.0 | Not applicable: Qt WebEngine has no extension host; it is not a library. |
| NØR built-in C++ engine | C++ | GPL-3.0-or-later (this project) | Kept as the zero-dependency fallback build path and as the reference semantics for the documented Adblock subset (`docs/FILTER_RULES.md`). |

Deciding measurements (real runs, this repository's test suite):

- Engine build from real EasyList + EasyPrivacy (132,207 lines):
  **107 ms** parse, **+14.4 MB** RSS, **1.38–3 µs** per decision.
- Serialized engine cache: **5.9 MB**, deserialize **2 ms** (vs 107 ms
  re-parse) — the startup cache rationale.
- Correctness on real lists: doubleclick/google-analytics/googlesyndication
  requests blocked; first-party content allowed (verified in
  `tst_rustengine::easyListIntegration`).

## 2. Architecture

```
RequestInterceptor (per WebEngine profile, IO thread)
        │ decide(url, firstParty, type) ──────────── thread-safe (locks)
        ▼
AdBlockClient (orchestrator: allowlist, per-site stats, policy gates)
        │ AdblockBackend interface (swappable adapter)
        ├── RustAdblockBackend      ── C ABI ──► adblock-sys (Rust shim)
        │                                          └── adblock 0.13.5 crate
        └── BuiltInAdblockBackend  (FilterRule C++ engine, fallback)

FilterListManager (downloads, ETag/304, validation, offline cache,
                   serialized engine cache with input-hash sidecar)

WebPage (GUI thread) ── cosmeticResources(url) / genericHideSelectors(...)
        └── QWebEngineScript "nor-cosmetic-filters" @ DocumentCreation
            + late pass after loadFinished (generic + first-load race)
```

Key properties:

- **Adapter isolation**: `AdBlockClient`'s public interface is unchanged
  since v1.0.0; backends implement `AdblockBackend`. The built-in engine
  remains fully functional (`-DNOR_ADBLOCK_BACKEND=BuiltIn`), and both
  backends pass the same behavioural test suites.
- **Threading**: `interceptRequest` runs on WebEngine's IO thread while
  reload/cosmetic calls run on the GUI thread. The Rust engine is built
  with Send+Sync (the `single-thread` cargo feature is deliberately NOT
  enabled), the C shim serializes every call, and the C++ side guards the
  backend pointer with a read-write lock. Statistics use a dedicated
  mutex (this also fixed a latent v1.0.0 race on the blocked counter).
- **Failsafe FFI**: every entry point in `adblock-sys/src/lib.rs` wraps
  its body in `catch_unwind`; a Rust panic maps to an error code and
  never unwinds into the C++ host.
- **Cosmetic filtering policy**: CSS-only. `hide_selectors` and
  `exceptions` from `url_cosmetic_resources()` are injected as a
  `<style>` element through `QWebEngineScript` (ApplicationWorld, runs on
  subframes). Generic `##` selectors are matched against the classes/ids
  actually present on the page via a DOM probe. `injected_script`
  (scriptlets from filter lists) is intentionally ignored: executing
  third-party scriptlets would weaken page isolation for no essential
  function.
- **No coupling to the shipped binary**: the shim is a plain C ABI
  (`adblock-sys/include/nor_adblock.h`); replacing or removing the Rust
  backend requires no changes outside `src/privacy/`.

## 3. Build integration

```bash
# default: Rust backend when a cargo toolchain is found
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.x/gcc_64
# explicit choices:
cmake -B build -GNinja -DNOR_ADBLOCK_BACKEND=Rust ...
cmake -B build -GNinja -DNOR_ADBLOCK_BACKEND=BuiltIn ...
# cargo not on PATH? point at it:
export NOR_CARGO=/path/to/cargo
```

The `nor_adblock_sys` custom target invokes cargo (pinned toolchain via
`adblock-sys/rust-toolchain.toml`, pinned dependency via
`adblock-sys/Cargo.toml`); the resulting static library is linked into
the browser and the test executables. No absolute paths are committed.

## 4. Data flow for lists

1. Startup: `FilterListManager::initialize()` validates the serialized
   engine cache against sha256 hashes of every input (built-in list,
   custom files, downloaded caches). Match → deserialize (fast path);
   mismatch/corrupt → re-parse and re-serialize.
2. ~1.5 s after the first window appears: `updateAll()` (when
   auto-update is on) downloads each configured HTTPS list with
   If-None-Match/If-Modified-Since. 304 → no-op. 200 → validate
   (`isPlausibleFilterList`: UTF-8, size bounds, ≥50 % rule-like lines,
   no bare HTML error pages) → atomic tmp+rename replace → engine
   rebuild → cache refresh.
3. Offline or server failure: previous cache stays, engine unchanged,
   warnings recorded; the browser keeps working with whatever it has
   (worst case: the built-in starter list).

Privacy note: enabling filter-list updates means the browser's IP address
is seen by the list host (easylist.to by default) at update time, like
any browser with updatable blocklists. Users who prefer not to can turn
off automatic updates and manage lists manually (Settings → Privacy, or
remove the URLs entirely).
