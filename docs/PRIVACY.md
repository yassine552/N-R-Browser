# NØR Browser Privacy Statement (docs/PRIVACY.md)

This document describes, precisely and honestly, what NØR Browser does
with your data.

## What the browser itself does

- **No account** is required for anything.
- **No telemetry, analytics, or crash reporting** is built in. The
  browser never phones home: there is no application-owned server.
- **No cloud sync or cloud storage.** Bookmarks, history, settings,
  permissions and download records live in local files only:
  - Linux: `~/.local/share/nor-browser/` (honor `XDG_*` variables)
  - Windows: `%LOCALAPPDATA%\nor-browser\`
  - Preferences: `QSettings` (INI) under the OS config directory.
- The **built-in blocklist and any custom lists are read from local
  files**. Nothing about your browsing is ever sent to a list provider;
  if you add a third-party list you downloaded yourself, updating it is
  your manual action. The default EasyList/EasyPrivacy downloads contact
  `easylist.to` directly (filter-list updater user agent, no cookies,
  no identifiers) and only when automatic updates are enabled.
- The **new-tab shortcuts** are a static curated list plus "frequently
  visited" tiles computed from your **local** history.

## Privacy hardening (Settings → Privacy)

All of the following are ON by default and individually switchable;
  every one of them is verified by an automated end-to-end test
  (`--smoke-privacy`):

- **Third-party cookies are blocked** through the WebEngine cookie-store
  filter: a cookie whose origin belongs to a different site than the page
  you are on is rejected before it is stored. First-party cookies keep
  working (tested).
- **Do Not Track (`DNT: 1`) and Global Privacy Control (`Sec-GPC: 1`)**
  are sent on every http/https request. These are legally meaningful
  opt-out signals (e.g. GDPR/CCPA contexts), not anonymity tools; they
  also mark the request as coming from a privacy-conscious browser.
- **Cookies can be made session-only** ("keep cookies until NØR closes"):
  nothing is persisted to disk across restarts.
- **Unified Chromium User-Agent:** the default Qt WebEngine agent includes
  a `QtWebEngine/6.8.x` token that makes every Qt-based browser trivially
  identifiable. NØR presents the plain Chromium agent matching the real
  engine version (nothing is faked or randomized — this removes a
  version-tag, it does not claim anonymity).
- **WebRTC exposes only the default public network interface** to pages
  (`--webrtc-ip-handling-policy=default_public_interface_only`), so pages
  cannot enumerate local interfaces/IPv6 addresses through RTCPeerConnection
  candidates. Applies at startup (a restart is needed after toggling).
- **Mixed (insecure) content never runs inside HTTPS pages**
  (`AllowRunningInsecureContent` = off) and **media autoplay requires a
  user gesture** (`PlaybackRequiresUserGesture` = on; also saves CPU).
- Hyperlink auditing (`<a ping>` beacons) is always disabled.

## What the browser cannot change (network reality)

Visiting a website means **your requests reach that website** and the
services it embeds. NØR Browser's ad/tracker blocking and Data Saver
remove a large share of those third-party requests, but:

- The sites you visit see your IP address, browser characteristics
  (user agent) and the pages you request.
- Search engines see your search queries (DuckDuckGo is the default
  precisely because it does not build a search-profile on you).
- **Private browsing is not anonymity.** It keeps the session's cookies,
  cache and history off your device — it does not hide you from
  networks, sites, ISPs, or employers.

## Fingerprinting: what is mitigated and what is not

Honest assessment (see also `docs/KNOWN_LIMITATIONS.md`):

- **Mitigated:** the `QtWebEngine` User-Agent tag (removed, unified
  across all NØR users on the same version); WebRTC local-interface
  enumeration; hyperlink-audit beacons; a large share of the tracking
  scripts that would run fingerprinting code in the first place
  (EasyList + EasyPrivacy block the loader before it executes).
- **NOT mitigated (a Chromium hard-fork would be required):** canvas,
  WebGL, audio and font fingerprints remain measurable by any site that
  gets its JavaScript to run. Qt WebEngine exposes no API to farble
  (randomize) those APIs the way Brave/Firefox RFP do. Screen size,
  timezone, languages and platform hints are likewise reported by the
  engine. Any browser claiming otherwise on this engine would be
  lying; NØR prefers to reduce and unify what it honestly can.

## Data deletion

Tools → Clear browsing data… removes, immediately and locally:

- browsing history (SQLite),
- cookies and site sign-ins,
- HTTP cache and visited-link history.

“All persistent site storage” (local storage, databases, service
workers — data the engine cannot wipe safely while running) is flagged
and removed **before the engine starts on the next launch**.

## Security posture

- TLS certificate errors are **never bypassed automatically**; only an
  explicit user decision can proceed past an overridable error, and
  non-overridable errors are always rejected.
- Chromium's sandbox and site-isolation stay enabled (the only
  exception is the documented test-environment flag used by the CI
  smoke runner inside a locked-down container).
- Navigations to `file://` initiated by web content are blocked.
- All SQLite statements are prepared with bound values — no query string
  concatenation anywhere.
- Downloads never silently overwrite an existing file; duplicate names
  get a numbered suffix.
- Sensitive browsing data is not logged. Log output is limited to
  error/diagnostic messages without URLs in normal operation.
