# NØR Browser

**NØR Browser** is a private-by-design desktop web browser built with modern
C++17 and Qt 6.8 / Qt WebEngine (Chromium). It requires no account, has no
telemetry, and keeps all of your data — preferences, bookmarks, history,
downloads — on your own device.

The interface ships in **English (default)**, **Arabic (full RTL)**, and
**French**, with a runtime language switcher and an architecture built for
adding more languages.

| | |
|---|---|
| Product name | NØR Browser |
| Executable / CMake target | `nor-browser` |
| Version | 1.0.0 |
| Language | C++17 |
| UI toolkit | Qt 6.8 Widgets |
| Rendering | Qt WebEngine (Chromium) |
| Storage | QSettings + SQLite (WAL) |
| Platforms | Linux (incl. PikaOS), Windows 10/11 |

---

## 1. Features

### Real browsing
- HTTP/HTTPS rendering through Qt WebEngine (Chromium).
- Tabs: multiple independent tabs, duplicate, close others/right, per-tab
  context menu, keyboard selection (`Ctrl+1..9`).
- The window deliberately has **no permanent status bar**: the page keeps
  the full viewport (link-hover URLs appear in a small floating overlay
  that takes no layout space). Every page is painted with the theme
  background color, which removes the white flash when switching tabs.
- **Modern browser chrome**: the tab strip sits above the navigation row,
  the classic menu bar is replaced by a hamburger menu, and the address
  bar is the centerpiece of the navigation row. The bookmarks row only
  appears when bookmarks exist.
- **Design system ("calm graphite, one violet pulse")**: every color,
  metric and radius is a design token in `src/ui/Theme.h`; Dark and Light
  themes are generated from one QSS template (`chrome.qss.in`). Choose
  *Match my desktop theme / Dark / Light* in Settings. On KDE/X11 with a
  compositor the header strip gets real blur-behind translucency; without
  one the browser stays fully opaque (no guessing, no artifacts).
  Icons are recolored per theme from one homogeneous SVG set.
- Navigation: back / forward / reload / stop / home, history-aware button
  states, thin progress bar.
- Address bar: URL vs. search detection, international (Unicode/IDN)
  input, `Paste and Go`, security glyph (lock for HTTPS).
- Search: **DuckDuckGo is the default engine**; Google, Bing and Wikipedia
  are preconfigured. Engines are fully editable (name + URL template) in
  Settings → Search, including the default choice.
- Custom new-tab page (`nor://newtab`): search field plus **Top Sites** —
  a Firefox-style grid of the sites you actually visit most, derived from
  the local history with a frecency ranking (visit count with a 14-day
  half-life), grouped one tile per host. Pin, unpin, edit and remove are
  available from each tile's context menu; sites without a favicon get a
  clean letter tile. No fixed/curated shortcuts, no external service:
  everything is computed from local data, and private windows render the
  list read-only so a private session can never change it.
- Find-in-page bar with live match counter (`Ctrl+F`).
- Zoom: `Ctrl+Plus/Minus/0` and Ctrl+wheel; the current zoom level shows
  as a small button next to the address bar (click to reset).
- Full-screen mode (site-initiated and F11).
- Download handling: progress, completion, failure and cancel states,
  non-overwriting unique file names, open-after-download, destination
  dialog (or a configured default folder).
- Renderer-crash overlay with one-click reload.

### Privacy & security
- **No accounts, no telemetry, no cloud sync.**
- Genuine private browsing: a separate off-the-record WebEngine profile
  that never writes to history and is wiped when the last private window
  closes (verified by tests).
- **Ad/tracker blocking powered by [brave/adblock-rust](https://github.com/brave/adblock-rust)
  0.13.5** (MPL-2.0, pinned) behind a swappable backend adapter, with the
  built-in C++ engine retained as a zero-dependency fallback
  (`-DNOR_ADBLOCK_BACKEND=BuiltIn`). Full EasyList/EasyPrivacy syntax:
  `$domain=` rules (with `~` negation), `$match-case` regex filters,
  exceptions, public-suffix-correct registrable domains (co.uk…), and
  **cosmetic filtering** (CSS selector element hiding, injected through
  QtWebEngine user scripts — CSS only, filter-list scriptlets are never
  executed).
- Filter lists: official EasyList + EasyPrivacy are downloaded with
  conditional requests (ETag/304), validated before replacing the cached
  copy (corrupt or HTML-error downloads are discarded; EasyPrivacy's
  bare URL-substring rules are recognized), stored for
  offline use, and auto-updated in the background (disableable). A
  serialized engine cache makes startup skip re-parsing (~2 ms vs ~110 ms
  on the full pair; measured).
- Per-site blocking statistics in the address bar ("N on this site · M
  this session") plus the session-wide indicator.
- **Privacy hardening (all ON by default, all switchable, all
  end-to-end tested):** third-party cookies rejected through the
  cookie-store filter (first-party cookies unaffected), `DNT: 1` and
  `Sec-GPC: 1` signals on every request, session-only cookies option,
  unified plain-Chromium User-Agent (drops the `QtWebEngine/x.y`
  fingerprint tag), WebRTC restricted to the default public interface
  (no local-IP enumeration), mixed content blocked in secure pages,
  autoplay requires a user gesture. See `docs/PRIVACY.md` for an honest
  breakdown of what is — and is not — mitigated against fingerprinting.
- Data Saver mode for limited connections: blocks images per request
  (all images, or third-party only), speculative prefetches and pings,
  with **working per-site exceptions**.
- Local-only address-bar autocomplete from history and bookmarks
  (matching titles, URLs and visited hosts; works with Arabic and French
  text; never sends anything anywhere; can be disabled).
- Certificate errors are **never** accepted automatically; overridable
  errors show an explicit warning dialog. Self-signed endpoints are
  rejected in the automated tests.
- Site permission prompts (camera, microphone, geolocation, screen,
  notifications, clipboard, mouse lock, local fonts) with per-site memory
  and a review UI (Settings → Permissions).
- Clear-data dialog: history, cookies, cache/visited links immediately;
  persistent site storage on next restart (the wipe is only reported as
  done after the storage tree is actually removed — unit-tested).
- `file://` navigations initiated by web content are blocked.
- All SQLite access uses prepared statements only.

### Local data
- Bookmarks: add/edit/remove, bookmarks bar (hideable), quick star,
  manager dialog, open in current/new tab/new window.
- History: local SQLite store, search, per-entry and full deletion,
  recent menu; private windows never touch it. Clearing the history also
  refreshes Top Sites (derived entries vanish, pins survive like
  bookmarks) and wipes the local favicon cache.
- Downloads list with states and re-open.
- All paths respect the OS conventions
  (`~/.local/share/nor-browser` on Linux, `%LOCALAPPDATA%\nor-browser`
  on Windows).

### Internationalization
- English default and fallback; Arabic and French fully translated
  (221/221 strings each).
- Runtime switching from Settings; the whole UI — menus, tabs, dialogs,
  tooltips, status bar and the new-tab page — refreshes immediately.
- Arabic switches the application layout to right-to-left; controls,
  tabs, address field and dialogs mirror correctly.
- Dates and numbers are formatted per locale.
- See `docs/ADD_LANGUAGE.md` to add German, Spanish, Italian, …

---

## 2. Building on Linux

### Requirements
- GCC ≥ 11 or Clang ≥ 13 (tested with GCC 14.2, Debian 13)
- CMake ≥ 3.21
- Ninja (recommended) or Make
- Qt 6.8.x with modules: Widgets, WebEngineWidgets, WebEngineCore, Sql,
  Network, LinguistTools (lupdate/lrelease), Test
- **Optional (recommended): a Rust toolchain** (rustup, no root needed)
  for the adblock-rust engine backend. Without it the browser builds and
  runs with the built-in C++ engine. If cargo is not on `PATH`, point the
  build at it with the `NOR_CARGO` environment variable:

  ```bash
  # user-local rustup install (no root):
  curl -sSf https://sh.rustup.rs | sh -s -- --profile minimal --no-modify-path
  export NOR_CARGO=~/.cargo/bin/cargo   # or wherever rustup put cargo
  ```
- System libraries (Debian/Ubuntu package names):
  `libnss3 libasound2t64 libxkbcommon0 libxkbcommon-x11-0 libxcomposite1
  libxdamage1 libxrandr2 libgbm1 libatspi2.0-0 libcups2 libxtst6
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1
  libxcb-render-util0 libxcb-util1 libxcb-xkb1 libegl1 libopengl0
  libgl1`

On Debian/Ubuntu installing Qt is easiest through the system packages:

```bash
sudo apt install qt6-webengine-dev qt6-webengine-dev-tools \
     qt6-tools-dev qt6-tools-dev-tools libqt6sql6-sqlite \
     cmake ninja-build g++
```

or with the official Qt online installer (any Qt ≥ 6.8.0, GCC 64-bit).

### Configure, build, test, run

```bash
cd nor-browser
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.x/gcc_64
cmake --build build -j

# unit tests (Qt Test via CTest)
ctest --test-dir build --output-on-failure

# run
./build/nor-browser                 # normal window
./build/nor-browser --private       # private window
./build/nor-browser https://example.org
```

The build embeds translations and resources; the single executable is
self-contained apart from the Qt runtime.

### Smoke tests (end-to-end)

```bash
# starts the local test server + Xvfb automatically
tests/scripts/run_smoke_tests.sh build
```

The suite exercises: real page loads from a local server, multi-tab
navigation/back/forward, address-bar URL/search/international-query
semantics, private-profile separation and cleanup, EN→AR→FR switching
with RTL verification (plus a rendered-content check of the new-tab
page), a real download (progress, content, unique names), **cosmetic
filtering on a real page** (ad elements verified hidden, content
untouched), **network blocking of real tracker URLs** (doubleclick,
Google Analytics/Tag Manager, ScorecardResearch) with the REAL EasyList
+ EasyPrivacy lists loaded — both the engine decision and the live
interceptor, **privacy hardening** (DNT/GPC headers as received by the
server, unified User-Agent, third-party cookie blocking with a
first-party control), **TLS certificate rejection** (a local
self-signed HTTPS endpoint must fail while valid HTTPS still loads), and
a real HTTPS site (https://example.com).

Unit-test suites: `tst_adblock` (rule parsing + client), `tst_rustengine`
(adblock-rust compatibility, $domain/match-case/PSL, cosmetic resources,
serialization, real EasyList+EasyPrivacy integration), `tst_filterlists`
(downloads, ETag/304, corrupt-download rejection, offline, engine cache,
EasyPrivacy-style list validation), `tst_autocomplete`, `tst_stores`
(SQLite + integrity/recovery + site-data wipe + Data-Saver policy),
`tst_language`, `tst_profile` (off-the-record lifecycle, privacy
hardening application, User-Agent unification). Performance
measurements: `scripts/measure_perf.sh`.

> In locked-down containers the Chromium sandbox cannot start (no user
> namespaces). The smoke-test runner sets `QTWEBENGINE_DISABLE_SANDBOX=1`
> **for the test environment only** — the browser itself never disables
> the sandbox.

### Install (optional, user-level)

```bash
./packaging/linux/install.sh            # installs to ~/.local
# or system-wide with generated rules:
cmake --install build                   # /usr/local by default (root)
```

---

## 3. Building on Windows 10/11

Windows builds are fully supported by the code base, but were **not
executed in this environment** — see the step-by-step guide in
`docs/WINDOWS_BUILD.md` (Qt online installer, MSVC or MinGW, windeployqt,
icon, packaging).

---

## 4. Project layout

```
nor-browser/
├── CMakeLists.txt               # build + install + test registration
├── src/
│   ├── main.cpp                 # scheme registration, CLI, startup
│   ├── core/                    # AppConfig, LanguageManager, SearchEngines,
│   │                            # BookmarkStore, HistoryStore, DownloadManager,
│   │                            # Paths
│   ├── privacy/                 # FilterRule, AdBlockClient, RequestInterceptor
│   ├── browser/                 # ProfileManager, WebPage, WebView, TabWidget,
│   │                            # AddressBar, FindBar, NewTabSchemeHandler,
│   │                            # BrowserWindow, BrowserApplication
│   └── ui/                      # Theme, IconProvider, Settings/History/
│                                # Bookmarks/Downloads/ClearData dialogs
├── resources/                   # QSS theme, SVG icons, newtab HTML,
│                                # starter blocklist, Arabic fonts
├── translations/                # en / ar / fr  (.ts + generated .qm)
├── tests/                       # 4 Qt-Test suites + smoke scripts + test server
├── packaging/                   # Linux desktop entry/icons/install script,
│                                # Windows .rc + icon
├── docs/                        # FILTER_RULES, PRIVACY, ADD_LANGUAGE,
│                                # WINDOWS_BUILD, DEPENDENCIES
└── scripts/                     # icon generation, translation filling
```

### Architecture notes
- `ProfileManager` is the **single** owner of both WebEngine profiles
  (persistent regular + off-the-record private). Interceptors and scheme
  handlers are children of their profile, so they can never dangle.
- Deletion order is explicit: windows (→ pages) first, profiles second.
- `WebPage` centralizes page policy: certificates, permissions,
  popups→tabs, HTTP auth, history recording.
- The privacy engine (`FilterRule`/`AdBlockClient`) is pure QtCore and
  unit-tested without WebEngine.

---

## 5. Keyboard shortcuts

| Action | Shortcut |
|---|---|
| New tab / close tab | `Ctrl+T` / `Ctrl+W` |
| New window / private window | `Ctrl+N` / `Ctrl+Shift+P` |
| Duplicate tab | `Ctrl+Shift+K` |
| Select tab 1–8 / last | `Ctrl+1..8` / `Ctrl+9` |
| Focus address bar | `Ctrl+L` |
| Find in page | `Ctrl+F` |
| Bookmark page | `Ctrl+D` |
| History / Downloads | `Ctrl+H` / `Ctrl+J` |
| Zoom | `Ctrl+Plus / - / 0`, Ctrl+wheel |
| Back / Forward | `Alt+Left` / `Alt+Right` |
| Reload / Stop | `F5`, `Ctrl+R` / `Esc` |
| Full screen | `F11` |
| Quit | `Ctrl+Q` |

---

## 6. Documentation index

- `README.ar.md` — هذا الملف بالعربية.
- `docs/FILTER_RULES.md` — supported ad-block rule syntax and limits.
- `docs/PRIVACY.md` — exactly what the browser does and does not store.
- `docs/ADD_LANGUAGE.md` — adding a new UI language.
- `docs/WINDOWS_BUILD.md` — Windows build & packaging guide.
- `docs/DEPENDENCIES.md` — third-party components and licenses.

## 7. License

NØR Browser is **free software: you can use, study, share and modify it**
under the terms of the **GNU General Public License v3 or later**
(**GPL-3.0-or-later**) — see `LICENSE` for the full license text.

Every project source file (C++, Python, shell, QSS, HTML, CMake — plus
the starter blocklist, which carries an inline SPDX notice) carries an
`SPDX-License-Identifier: GPL-3.0-or-later` header, with two deliberate
exceptions: the `adblock-sys` C-ABI shim files (`adblock-sys/src/lib.rs`,
`adblock-sys/include/nor_adblock.h`) are **MPL-2.0** — original code kept
under the same license as the MPL-2.0 `adblock-rust` engine they wrap —
and they are compiled into and distributed with this larger
GPL-3.0-or-later work under **MPL-2.0 §3.3 ("Distribution of a Larger
Work")**, remaining under MPL-2.0 with their notices preserved and their
source shipped in this archive. (No secondary GPL license is claimed:
GPL-3.0 is not one of the Secondary Licenses defined by MPL-2.0 §1.12.)
Generated and translated assets (SVG icons, app icons, `.ts`
translations) and the documentation are covered by the project license
without per-file headers; the bundled new-tab backgrounds are dedicated
to the public domain (CC0 1.0) and the bundled Noto fonts are OFL-1.1.

Qt modules are not bundled in this archive and remain under the
licensing of The Qt Company Ltd — LGPL-3.0-only / GPL-3.0-only /
GPL-2.0-only / commercial for the QtBase modules used here, and
LGPL-3.0 / GPL-3.0 / GPL-2.0 / commercial for the Qt-specific parts of
Qt WebEngine; the Chromium portion embedded in Qt WebEngine carries its
own separate BSD-style third-party licenses. See
`THIRD_PARTY_NOTICES.md` and `docs/DEPENDENCIES.md` for the full
per-component breakdown.
