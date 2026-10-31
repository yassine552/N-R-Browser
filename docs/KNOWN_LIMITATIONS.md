# Known Limitations

An honest list of what NØR Browser does not (yet) do, updated for the
verified-integration release. Items resolved during this upgrade are
listed at the bottom for the record.

## Current limitations

1. **Fingerprinting protection is partial (engine-level limit).** Canvas,
   WebGL, audio and font fingerprints remain measurable by any site that
   gets JavaScript to run: Qt WebEngine offers no API to randomize
   ("farble") those surfaces, so NØR does not pretend to. What IS done:
   the `QtWebEngine/x.y` User-Agent tag is removed (unified plain-Chromium
   agent), WebRTC local-interface enumeration is blocked, DNT/GPC signals
   are sent, and the tracking scripts that perform fingerprinting are
   blocked by EasyList/EasyPrivacy before they load. See
   `docs/PRIVACY.md#fingerprinting-what-is-mitigated-and-what-is-not`.
2. **Cosmetic filtering: CSS-only, no scriptlets.** Filter-list
   scriptlets (`##^script:...`, `$redirect=...`, `injected_script`
   resources) are intentionally never executed — that would run
   third-party JavaScript in page context. Procedural cosmetic filters
   (`:has(...)`, `:has-text(...)` selectors) are parsed by the engine but
   not injected (only plain CSS selectors are). Effect: a small subset of
   anti-adblock circumvention filters is inactive.
3. **Site-storage clearing is still deferred to next start.** Qt
   WebEngine offers no public API to wipe IndexedDB/local-storage/service
   workers while the profile is live. The wipe now (a) provably completes
   (unit-tested: the storage tree is gone before the "cleared" claim) and
   (b) runs before the engine initializes. An in-session wipe is not
   available.
4. **Autocomplete is substring-based.** Matching is case-insensitive but
   not diacritic-insensitive: typing French without accents will not
   match titles stored with accents (and vice versa). Fuzzy/pinyin-style
   matching is out of scope.
5. **Filter-list updates need the network.** Offline behaviour is
   safe (cached lists, engine cache, built-in starter list) but obviously
   no new rules arrive. Update checks happen on start (plus manual
   "Update now"); no background scheduler during the session.
6. **Sandbox difference in the test environment only.** CI containers
   without user namespaces cannot start the Chromium sandbox, so the
   *test runners* set `QTWEBENGINE_DISABLE_SANDBOX=1` (and the cookie
   test additionally runs with `--ignore-certificate-errors` for the
   local self-signed endpoint). The browser binary itself never disables
   the sandbox or ignores certificate errors.
7. **Windows 10/11 support is untested.** The code builds with MSVC +
   Qt 6.8 in principle (CMake guards the platform-specific bits) but no
   actual Windows build/launch has been performed by the maintainers —
   see `docs/WINDOWS_BUILD.md`, including the Rust-toolchain notes.
8. **Per-site blocking statistics reset with the session.** The counts
   live in memory only (privacy: no additional persistent record of
   where you browse). They reset on restart and on "Clear browsing data".
9. **Engine debug info on large lists.** Matched-rule diagnostics are
   only kept for rule sets under 5,000 rules (memory); with full
   EasyList loaded, the UI shows counts, not which rule matched.
10. **The built-in fallback engine remains limited.** Without a Rust
    toolchain the browser falls back to the v1.0.0 C++ engine: no
    `$domain=`, no `$match-case`, no cosmetic filtering, simplified
    registrable-domain handling (documented in `docs/FILTER_RULES.md`).
11. **Single-locale filter lists.** Non-English list variants (e.g.
    Arabic/French supplemental lists) can be added as URLs in Settings
    but there is no per-locale default or negotiation.
12. **Third-party cookie scope is per-profile, not per-site.** The
    cookie filter applies browser-wide (matching what Chromium does with
    its own third-party-cookie setting); there is no per-site exception
    UI for cookies yet.

## Resolved in this release (for the record)

- ✅ v1.1 #1 *EasyPrivacy was silently discarded as "implausible content"*
  → the download validator now recognizes bare URL-substring rules
  ("-tracker.", ".beacon.min.js", "&ev=PageView&"…) that dominate
  EasyPrivacy. Verified with a real download: both lists cache
  (133,588 rules) and the regression test ships in `tst_filterlists`.
- ✅ v1.1 #2 *new-tab "Frequently visited" heading broke the tile grid*
  (injected as a flex item between tiles; visible in RTL) → it is now a
  separate section with its own centered tile row; the curated six
  shortcuts render as one even grid row (verified visually + via the
  `lang/newtab-renders` smoke check).
- ✅ v1.1 #3 *broken bundled Arabic fonts* (HTML error pages saved as
  .ttf) → replaced with genuine Noto Sans/Naskh Arabic (OFL 1.1, license
  bundled in `resources/fonts/OFL.txt`).
- ✅ v1.1 #4 *data races on Qt WebEngine's IO thread* (QSettings reads
  from the request interceptor and the ad-block client) → all
  IO-thread-visible policy is now snapshotted under mutexes and
  refreshed on the GUI thread.
- ✅ v1.1 #5 *smoke-test runner counted crashed modes as passed*
  (PIPESTATUS captured `true` instead of the browser exit code) → the
  runner now captures the real exit code AND fails any mode that
  produced no `SMOKE RESULT` output.
- Also fixed en route in v1.1: no third-party cookie blocking at all
  (added via the Qt 6.8 cookie-store filter), no DNT/GPC signals, no
  unified User-Agent, no WebRTC IP minimization, mixed-content and
  autoplay hardening, and two new end-to-end smoke modes
  (`network-blocking` with the real EasyList: 15 checks; `privacy-
  hardening`: 7 checks).
