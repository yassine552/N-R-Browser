// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "NewTabSchemeHandler.h"

#include "AppConfig.h"
#include "BackgroundStore.h"
#include "FaviconStore.h"
#include "HistoryStore.h"
#include "SearchEngines.h"
#include "TopSitesStore.h"
#include "Theme.h"

#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

#include <algorithm>

using namespace std::chrono_literals;

namespace {
// Dedicated translation context for the built-in new-tab page.
QString NTP(const char *source)
{
    return QCoreApplication::translate("NewTabPage", source);
}

// JSON embedded in a <script> block must never contain a literal "<"
// (a page title like "</script><b>x" would break out of the script).
QString scriptSafe(const QString &json)
{
    QString out = json;
    out.replace(QLatin1Char('<'), QStringLiteral("\\u003c"));
    out.replace(QLatin1Char('>'), QStringLiteral("\\u003e"));
    out.replace(QLatin1Char('&'), QStringLiteral("\\u0026"));
    return out;
}

// Bundled "popular sites" completion list for the New Tab search field:
// plain facts (a site's name and its address), shipped in the binary. The
// matching itself runs client-side in the page — typing "yout" offers
// "youtube.com" WITHOUT a single network request, so no keystroke ever
// leaves the machine (the same privacy contract as the rest of NØR).
struct PopularSite
{
    const char *name;
    const char *url;
};
const PopularSite kPopularSites[] = {
    { "YouTube", "https://www.youtube.com/" },
    { "Wikipedia", "https://www.wikipedia.org/" },
    { "Google", "https://www.google.com/" },
    { "GitHub", "https://github.com/" },
    { "DuckDuckGo", "https://duckduckgo.com/" },
    { "Reddit", "https://www.reddit.com/" },
    { "X (Twitter)", "https://x.com/" },
    { "Stack Overflow", "https://stackoverflow.com/" },
    { "ChatGPT", "https://chatgpt.com/" },
    { "Claude", "https://claude.ai/" },
    { "Facebook", "https://www.facebook.com/" },
    { "Instagram", "https://www.instagram.com/" },
    { "TikTok", "https://www.tiktok.com/" },
    { "LinkedIn", "https://www.linkedin.com/" },
    { "Netflix", "https://www.netflix.com/" },
    { "Amazon", "https://www.amazon.com/" },
    { "AliExpress", "https://www.aliexpress.com/" },
    { "Spotify", "https://open.spotify.com/" },
    { "Twitch", "https://www.twitch.tv/" },
    { "Brave Search", "https://search.brave.com/" },
    { "MDN Web Docs", "https://developer.mozilla.org/" },
    { "Hacker News", "https://news.ycombinator.com/" },
    { "Gmail", "https://mail.google.com/" },
    { "Google Maps", "https://maps.google.com/" },
    { "Google Drive", "https://drive.google.com/" },
    { "WhatsApp Web", "https://web.whatsapp.com/" },
    { "Telegram Web", "https://web.telegram.org/" },
    { "Discord", "https://discord.com/" },
    { "Steam", "https://store.steampowered.com/" },
    { "Epic Games", "https://store.epicgames.com/" },
    { "Zoom", "https://zoom.us/" },
    { "Notion", "https://www.notion.so/" },
    { "Figma", "https://www.figma.com/" },
    { "Canva", "https://www.canva.com/" },
    { "Medium", "https://medium.com/" },
    { "Pinterest", "https://www.pinterest.com/" },
    { "eBay", "https://www.ebay.com/" },
    { "Booking.com", "https://www.booking.com/" },
    { "Airbnb", "https://www.airbnb.com/" },
    { "Tripadvisor", "https://www.tripadvisor.com/" },
    { "PayPal", "https://www.paypal.com/" },
    { "Yahoo", "https://www.yahoo.com/" },
    { "Bing", "https://www.bing.com/" },
    { "Brave Browser", "https://brave.com/" },
    { "Mozilla Firefox", "https://www.mozilla.org/" },
    { "VLC", "https://www.videolan.org/" },
    { "7-Zip", "https://www.7-zip.org/" },
    { "Cover Your Tracks", "https://coveryourtracks.eff.org/" },
    { "EFF", "https://www.eff.org/" },
    { "OBS Studio", "https://obsproject.com/" },
    { "Blender", "https://www.blender.org/" },
    { "Inkscape", "https://inkscape.org/" },
    { "GIMP", "https://www.gimp.org/" },
    { "Krita", "https://krita.org/" },
    { "Audacity", "https://www.audacityteam.org/" },
    { "HandBrake", "https://handbrake.fr/" },
    { "LibreOffice", "https://www.libreoffice.org/" },
    { "Qt", "https://www.qt.io/" },
    { "Rust", "https://www.rust-lang.org/" },
    { "Python", "https://www.python.org/" },
    { "Node.js", "https://nodejs.org/" },
    { "npm", "https://www.npmjs.com/" },
    { "Docker", "https://www.docker.com/" },
    { "W3Schools", "https://www.w3schools.com/" },
    { "freeCodeCamp", "https://www.freecodecamp.org/" },
    { "CodePen", "https://codepen.io/" },
    { "LeetCode", "https://leetcode.com/" },
    { "Khan Academy", "https://www.khanacademy.org/" },
    { "Coursera", "https://www.coursera.org/" },
    { "Udemy", "https://www.udemy.com/" },
    { "Duolingo", "https://www.duolingo.com/" },
    { "BBC", "https://www.bbc.com/" },
    { "CNN", "https://www.cnn.com/" },
    { "The Guardian", "https://www.theguardian.com/" },
    { "Al Jazeera", "https://www.aljazeera.net/" },
    { "France 24", "https://www.france24.com/" },
    { "Le Monde", "https://www.lemonde.fr/" },
    { "L'Équipe", "https://www.lequipe.fr/" },
    { "Deezer", "https://www.deezer.com/" },
    { "Baidu", "https://www.baidu.com/" },
    { "Yandex", "https://yandex.com/" },
    { "OpenStreetMap", "https://www.openstreetmap.org/" },
    { "Internet Archive", "https://archive.org/" },
    { "Wayback Machine", "https://web.archive.org/" },
    { "Speedtest", "https://www.speedtest.net/" },
    { "Let Me Google That", "https://letmegooglethat.com/" },
};

// A SHORT, honest display name for a suggestion row. Page titles are for
// tabs; a completion row needs "Browserleaks", never "Browserleaks - Check
// your browser for privacy leaks". Sites stack their name with taglines
// behind well-known separators — take the first segment and keep it only
// when it still looks like a NAME (short, single line). The bare host is
// always the honest fallback.
QString shortSiteName(const QString &title, const QString &host)
{
    const QString fallback = [&host]() {
        QString h = host.toLower();
        while (h.startsWith(QLatin1String("www.")))
            h.remove(0, 4);
        return h;
    }();

    QString t = title.trimmed();
    if (t.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || t.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
        || t.isEmpty()) {
        return fallback;
    }

    static const QStringList separators = {
        QStringLiteral(" - "), QStringLiteral(" | "), QStringLiteral(" · "),
        QStringLiteral(" — "), QStringLiteral(" – "), QStringLiteral(" · "),
        QStringLiteral(" :: "), QStringLiteral(" » "), QStringLiteral(" • "),
        QStringLiteral("–"), QStringLiteral("|"),
    };
    for (const QString &sep : separators) {
        const int i = t.indexOf(sep);
        if (i > 0) {
            t = t.left(i).trimmed();
            break;
        }
    }
    // A sentence, not a name: give up and show the host instead.
    if (t.isEmpty() || t.size() > 28 || t.contains(QLatin1Char('\n')))
        return fallback;
    return t;
}

// The user's own past queries, recovered OFFLINE from engine result-page
// URLs in the local history ("...?q=youtube+music" -> "youtube music").
// These power real query completions in the New Tab search field without
// a single byte leaving the machine. Empty when the URL carries no query.
QString extractEngineQuery(const QString &url, const QStringList &templates)
{
    const QUrl u(url);
    const QString base = TopSitesStore::baseDomainOf(u.host().toLower());
    if (base.isEmpty())
        return QString();
    for (const QString &tmpl : templates) {
        const QUrl tu(tmpl);
        if (TopSitesStore::baseDomainOf(tu.host().toLower()) != base)
            continue;
        const int qmark = tmpl.indexOf(QLatin1Char('?'));
        if (qmark < 0)
            continue;
        const QStringList pairs = tmpl.mid(qmark + 1).split(QLatin1Char('&'));
        for (const QString &pair : pairs) {
            if (!pair.contains(QLatin1String("%s")))
                continue;
            const QString key = pair.section(QLatin1Char('='), 0, 0);
            if (key.isEmpty())
                continue;
            QString value = QUrlQuery(u).queryItemValue(key);
            value.replace(QLatin1Char('+'), QLatin1Char(' '));
            value = value.simplified();
            if (value.size() >= 2 && value.size() <= 64
                && !value.startsWith(QLatin1String("http"))) {
                return value;
            }
            return QString();
        }
    }
    return QString();
}
} // namespace

NewTabSchemeHandler::NewTabSchemeHandler(HistoryStore *history, SearchEngines *search,
                                         TopSitesStore *topSites, FaviconStore *favicons,
                                         AppConfig *config, BackgroundStore *backgrounds,
                                         bool privateMode, QObject *parent)
    : QWebEngineUrlSchemeHandler(parent)
    , m_history(history)
    , m_search(search)
    , m_topSites(topSites)
    , m_favicons(favicons)
    , m_config(config)
    , m_backgrounds(backgrounds)
    , m_privateMode(privateMode)
{
}

NewTabSchemeHandler::NewTabSchemeHandler(HistoryStore *history, SearchEngines *search,
                                         QObject *parent)
    : QWebEngineUrlSchemeHandler(parent)
    , m_history(history)
    , m_search(search)
{
    // Compatibility constructor (tests): Top Sites degrade gracefully.
}

void NewTabSchemeHandler::registerNorScheme()
{
    // "nor" behaves like an internal scheme (qrc-like): not fetchable from
    // web content except our own pages, no CSP inheritance.
    QWebEngineUrlScheme scheme(QByteArrayLiteral("nor"));
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                    | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::ContentSecurityPolicyIgnored);
    QWebEngineUrlScheme::registerScheme(scheme);
}

QString NewTabSchemeHandler::escapeHtml(const QString &s)
{
    return s.toHtmlEscaped();
}

QString NewTabSchemeHandler::buildHtml() const
{
    QFile tpl(QStringLiteral(":/html/newtab.html"));
    if (!tpl.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("<html><body><h1>New Tab</h1></body></html>");
    QString html = QString::fromUtf8(tpl.readAll());

    const QString direction = QApplication::isRightToLeft() ? QStringLiteral("rtl")
                                                            : QStringLiteral("ltr");

    // Search engine metadata for the page's search field.
    const SearchEngine engine = m_search ? m_search->defaultEngine()
                                         : SearchEngine();
    QJsonObject engineJson;
    engineJson.insert(QStringLiteral("name"), engine.name);
    engineJson.insert(QStringLiteral("url"), engine.urlTemplate);
    const QString engineJsonStr = QString::fromUtf8(
        QJsonDocument(engineJson).toJson(QJsonDocument::Compact));

    // Top Sites: derived from the local history only (see TopSitesStore);
    // nothing leaves the machine, and no fixed/curated sites are injected.
    // In private windows the tiles render read-only: pinning, removing and
    // editing are hidden so a private session can never mutate the list.
    //
    // Engine result-page patterns: from the store when wired; the
    // compatibility constructor (tests) falls back to the engine list.
    QStringList engineTemplates;
    if (m_topSites)
        engineTemplates = m_topSites->engineUrlTemplates();
    else if (m_search) {
        const auto addEngine = [&engineTemplates](const SearchEngine &e) {
            if (e.isValid() && !engineTemplates.contains(e.urlTemplate))
                engineTemplates.append(e.urlTemplate);
        };
        addEngine(m_search->defaultEngine());
        for (const SearchEngine &e : m_search->engines())
            addEngine(e);
    }

    // Titles that are really URLs (a desynced history row, e.g. the search
    // page's title landing on the destination entry) make unreadable
    // labels — the host is always the honest fallback.
    auto displayTitle = [](const QString &title, const QString &host) {
        const QString t = title.trimmed();
        if (t.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || t.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
            return host;
        return t;
    };

    QJsonArray sites;
    const QVector<TopSite> top = m_topSites
        ? m_topSites->topSites(kTopSitesLimit) : QVector<TopSite>();
    // Titles the page may render, keyed by url: sanitized once, reused for
    // both tiles and suggestions so the two never disagree.
    QHash<QString, QString> cleanTitles;
    {
        // Last-resort duplicate guard: whatever the stores produce (old
        // state files, exotic hosts, future regressions), the page NEVER
        // renders two tiles for one site — and never two tiles that look
        // pixel-identical (same base domain, or same visible title).
        QSet<QString> seenBases;
        QSet<QString> seenTitles;
        for (const TopSite &s : top) {
            const QString base = s.baseDomain.isEmpty()
                ? TopSitesStore::baseDomainOf(s.host) : s.baseDomain;
            const QString title = displayTitle(s.title, s.host);
            const QString normTitle = title.toLower();
            if (!base.isEmpty() && seenBases.contains(base))
                continue;
            if (!normTitle.isEmpty() && seenTitles.contains(normTitle))
                continue;
            seenBases.insert(base);
            seenTitles.insert(normTitle);
            cleanTitles.insert(s.url, title);
            QJsonObject o;
            o.insert(QStringLiteral("url"), s.url);
            o.insert(QStringLiteral("title"), title);
            o.insert(QStringLiteral("host"), s.host);
            o.insert(QStringLiteral("pinned"), s.pinned);
            // Favicon: the representative host, or the first host of the
            // base-domain chain that actually has a cached icon (the tile
            // may be represented by "x.com" while the icon was captured
            // on "www.x.com").
            QString favicon;
            if (m_favicons) {
                if (m_favicons->hasIcon(s.host)) {
                    favicon = FaviconStore::iconUrlFor(s.host);
                } else {
                    for (const QString &alt : s.faviconHosts) {
                        if (alt != s.host && m_favicons->hasIcon(alt)) {
                            favicon = FaviconStore::iconUrlFor(alt);
                            break;
                        }
                    }
                }
            }
            o.insert(QStringLiteral("favicon"), favicon);
            sites.append(o);
        }
    }
    const QString sitesJson = scriptSafe(
        QString::fromUtf8(QJsonDocument(sites).toJson(QJsonDocument::Compact)));

    // ---- search suggestions ------------------------------------------------
    // Offline completion source for the New Tab search field: the user's own
    // Top Sites (they earned their place), then the bundled popular-sites
    // list, then the best hosts from the LOCAL history. Favicons ride along
    // when one is already cached. The page matches prefixes client-side —
    // typing "yout" offers "youtube.com" with zero network traffic. In
    // private mode the history block is skipped entirely (a private session
    // must not even READ the real profile's history into a suggestion UI).
    // Row names are SHORT site names (shortSiteName): a completion row reads
    // "Browserleaks — browserleaks.com", never a full page title, and the
    // page matches against name+host only — irrelevant rows can no longer
    // sneak in through title substrings.
    QJsonArray suggest;
    QJsonArray pastQueries;   // ranked offline query completions
    {
        auto appendEntry = [&suggest, this](const QString &name, const QString &url,
                                            const QString &host) {
            QJsonObject o;
            o.insert(QStringLiteral("name"), name);
            o.insert(QStringLiteral("url"), url);
            o.insert(QStringLiteral("host"), host);
            QString fav;
            if (m_favicons && m_favicons->hasIcon(host))
                fav = FaviconStore::iconUrlFor(host);
            o.insert(QStringLiteral("fav"), fav);
            suggest.append(o);
        };
        for (const TopSite &s : top) {
            // Tiles keep their full titles (CSS truncates them); suggestion
            // rows carry the SHORT name so the dropdown stays scannable.
            const QString title = cleanTitles.value(s.url,
                displayTitle(s.title, s.host));
            appendEntry(shortSiteName(title, s.host), s.url, s.host);
        }
        for (const PopularSite &p : kPopularSites) {
            const QUrl u(QString::fromLatin1(p.url));
            appendEntry(QString::fromLatin1(p.name), u.toString(), u.host());
        }
        if (!m_privateMode && m_history) {
            // Best entry per visited host, ranked by frecency, capped: the
            // completion list stays small even with a long history.
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            struct HostEntry
            {
                QString title, url, host;
                qreal score = 0.0;
            };
            QHash<QString, HostEntry> byHost;
            // Past searches: the user's own queries recovered from engine
            // result-page URLs, ranked by frecency — REAL query completions
            // with zero network traffic.
            struct PastQuery
            {
                qreal score = 0.0;
                QString display;
            };
            QHash<QString, PastQuery> byQuery;
            const QVector<HistoryEntry> entries = m_history->allEntries();
            for (const HistoryEntry &e : entries) {
                const QString host = TopSitesStore::hostOf(e.url);
                if (host.isEmpty())
                    continue;
                // Engine result pages ("query at DuckDuckGo") are skipped:
                // the popular-sites list already offers the engine itself,
                // cleanly named — and this keeps titles like
                // "https://iphey.com/ at DuckDuckGo" out of the dropdown.
                // The query they carry, though, becomes a completion.
                if (!engineTemplates.isEmpty()
                    && TopSitesStore::isEnginePageUrl(e.url, engineTemplates)) {
                    const QString text = extractEngineQuery(e.url, engineTemplates);
                    if (!text.isEmpty()) {
                        const qreal qs = TopSitesStore::frecencyScore(e.visitCount,
                                                                    e.lastVisit, now);
                        PastQuery &pq = byQuery[text.toLower()];
                        pq.score += qs;
                        if (pq.display.isEmpty())
                            pq.display = text;
                    }
                    continue;
                }
                const qreal score = TopSitesStore::frecencyScore(e.visitCount, e.lastVisit, now);
                auto it = byHost.find(host);
                if (it == byHost.end()) {
                    HostEntry he;
                    he.title = e.title.isEmpty() ? host : e.title;
                    he.url = e.url;
                    he.host = host;
                    he.score = score;
                    byHost.insert(host, he);
                } else if (score > it.value().score) {
                    it.value().title = e.title.isEmpty() ? host : e.title;
                    it.value().url = e.url;
                    it.value().score = score;
                }
            }
            QVector<HostEntry> rankedHosts;
            rankedHosts.reserve(byHost.size());
            for (const auto &he : byHost)
                rankedHosts.append(he);
            std::sort(rankedHosts.begin(), rankedHosts.end(),
                      [](const HostEntry &a, const HostEntry &b) { return a.score > b.score; });
            for (int i = 0; i < rankedHosts.size() && i < 40; ++i) {
                const HostEntry &he = rankedHosts.at(i);
                appendEntry(shortSiteName(he.title, he.host), he.url, he.host);
            }
            // Ranked past queries for the page's query-completion rows.
            {
                struct ScoredQuery
                {
                    qreal score = 0.0;
                    QString display;
                };
                QVector<ScoredQuery> rankedQueries;
                rankedQueries.reserve(byQuery.size());
                for (const auto &pq : byQuery)
                    rankedQueries.append({ pq.score, pq.display });
                std::sort(rankedQueries.begin(), rankedQueries.end(),
                          [](const ScoredQuery &a, const ScoredQuery &b) {
                              return a.score > b.score;
                          });
                for (int i = 0; i < rankedQueries.size() && i < 30; ++i)
                    pastQueries.append(rankedQueries.at(i).display);
            }
        }
    }
    const QString suggestJson = scriptSafe(
        QString::fromUtf8(QJsonDocument(suggest).toJson(QJsonDocument::Compact)));
    const QString queriesJson = scriptSafe(
        QString::fromUtf8(QJsonDocument(pastQueries).toJson(QJsonDocument::Compact)));
    // Always replaced — even when empty — so the page's JS never sees the
    // raw placeholder (which would be a syntax error).
    html.replace(QStringLiteral("{{QUERIES_JSON}}"), queriesJson);

    // New Tab customization preferences + background gallery, injected as
    // one JSON block. Defaults mirror AppConfig when the compatibility
    // constructor (tests) left the config unset.
    {
        QJsonObject prefs;
        const bool bgOn = m_config && m_backgrounds
                          && m_config->newTabBackgroundEnabled();
        QString bgId = m_config ? m_config->newTabBackgroundId() : QString();
        if (bgOn && !m_backgrounds->contains(bgId))
            prefs.insert(QStringLiteral("bgEnabled"), false); // stale id: flat color
        else
            prefs.insert(QStringLiteral("bgEnabled"), bgOn);
        if (bgOn) {
            prefs.insert(QStringLiteral("bgUrl"),
                         QStringLiteral("nor://background/") + bgId);
            prefs.insert(QStringLiteral("bgDark"),
                         m_backgrounds->entry(bgId).dark);
        }
        prefs.insert(QStringLiteral("blur"),
                     m_config ? m_config->newTabBlurEnabled() : true);
        prefs.insert(QStringLiteral("searchAlpha"),
                     (m_config ? m_config->newTabSearchOpacity() : 48) / 100.0);
        prefs.insert(QStringLiteral("showTopSites"),
                     m_config ? m_config->newTabShowTopSites() : true);
        if (m_backgrounds) {
            QJsonArray gallery;
            for (const BackgroundStore::Entry &e : m_backgrounds->entries()) {
                QJsonObject g;
                g.insert(QStringLiteral("id"), e.id);
                g.insert(QStringLiteral("name"), e.name);
                g.insert(QStringLiteral("thumb"),
                         QStringLiteral("nor://background/thumb/") + e.id);
                g.insert(QStringLiteral("dark"), e.dark);
                gallery.append(g);
            }
            prefs.insert(QStringLiteral("gallery"), gallery);
        }
        html.replace(QStringLiteral("{{NEWTAB_JSON}}"),
                     scriptSafe(QString::fromUtf8(
                         QJsonDocument(prefs).toJson(QJsonDocument::Compact))));
    }

    const QString placeholders = engine.isValid()
        ? NTP(QT_TRANSLATE_NOOP("NewTabPage", "Search with %1")).arg(engine.name)
        : NTP(QT_TRANSLATE_NOOP("NewTabPage", "Search the web"));
    // Same string, pre-computed for the suggestion dropdown's search row —
    // embedded in a JS string literal, so quotes/backslashes are escaped
    // (engine names are user-editable in Settings).
    QString searchWith = engine.isValid()
        ? NTP(QT_TRANSLATE_NOOP("NewTabPage", "Search with %1")).arg(engine.name)
        : NTP(QT_TRANSLATE_NOOP("NewTabPage", "Search the web"));
    searchWith.replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
        .replace(QLatin1Char('"'), QStringLiteral("\\\""));

    // One design system: the page receives the SAME tokens the chrome
    // stylesheet uses (Theme::palette), so theme switches are coherent.
    {
        const Theme::Palette &p = Theme::palette();
        // NOTE: negative alpha = "use the color's own alpha". The palette
        // carries translucency inside the QColor (hover overlays, hair
        // lines); an always-1.0 default would stomp it and render tile
        // hovers as opaque white.
        auto rgba = [](const QColor &c, qreal a = -1.0) {
            QColor t = c;
            if (a >= 0.0)
                t.setAlphaF(a);
            return QStringLiteral("rgba(%1,%2,%3,%4)")
                .arg(t.red()).arg(t.green()).arg(t.blue())
                .arg(t.alphaF(), 0, 'f', 3);
        };
        const QString css = QStringLiteral(
            ":root{--bg:%1;--surface:%2;--elevated:%3;"
            "--text:%4;--dim:%5;--muted:%6;--line:%7;--line-strong:%8;"
            "--accent:%9;--accent-hover:%10;--danger:%11;--hover:%12;--letter-bg:%13;}")
            .arg(rgba(p.window), rgba(p.surface), rgba(p.elevated),
                 rgba(p.text), rgba(p.textDim), rgba(p.textMuted),
                 rgba(p.line), rgba(p.lineStrong),
                 rgba(p.accent), rgba(p.accentHover), rgba(p.danger),
                 rgba(p.hoverOverlay), rgba(p.hoverOverlay));
        html.replace(QStringLiteral("{{PALETTE_CSS}}"), css);
    }
    html.replace(QStringLiteral("{{DIR}}"), direction);
    html.replace(QStringLiteral("{{LANG}}"), direction == QLatin1String("rtl")
                                             ? QStringLiteral("ar") : QStringLiteral("en"));
    // Theme class drives the solid-color (no background) glass scheme.
    html.replace(QStringLiteral("{{THEME_CLASS}}"),
                 Theme::isDark() ? QStringLiteral("theme-dark")
                                 : QStringLiteral("theme-light"));
    html.replace(QStringLiteral("{{TITLE}}"), NTP(QT_TRANSLATE_NOOP("NewTabPage", "New Tab")));
    html.replace(QStringLiteral("{{GREETING}}"), NTP(QT_TRANSLATE_NOOP("NewTabPage", "Where to next?")));
    html.replace(QStringLiteral("{{SEARCH_PLACEHOLDER}}"), placeholders);
    html.replace(QStringLiteral("{{SEARCH_WITH}}"), searchWith);
    html.replace(QStringLiteral("{{SEARCH_BUTTON}}"), NTP(QT_TRANSLATE_NOOP("NewTabPage", "Search")));
    html.replace(QStringLiteral("{{TOPSITES_TITLE}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Top sites")));
    html.replace(QStringLiteral("{{EMPTY_HINT}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Sites you visit most will appear here.")));
    html.replace(QStringLiteral("{{MENU_OPEN_NEWTAB}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Open in new tab")));
    html.replace(QStringLiteral("{{MENU_OPEN_NEWWINDOW}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Open in new window")));
    html.replace(QStringLiteral("{{MENU_PIN}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Pin")));
    html.replace(QStringLiteral("{{MENU_UNPIN}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Unpin")));
    html.replace(QStringLiteral("{{MENU_EDIT}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Edit…")));
    html.replace(QStringLiteral("{{MENU_REMOVE}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Remove from Top Sites")));
    html.replace(QStringLiteral("{{EDIT_DIALOG_TITLE}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Edit site")));
    html.replace(QStringLiteral("{{EDIT_LABEL_TITLE}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Name")));
    html.replace(QStringLiteral("{{EDIT_LABEL_URL}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Address")));
    html.replace(QStringLiteral("{{EDIT_SAVE}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Save")));
    html.replace(QStringLiteral("{{EDIT_CANCEL}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Cancel")));
    html.replace(QStringLiteral("{{EDIT_INVALID_URL}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Enter a valid http(s) address.")));
    // Customization panel strings.
    html.replace(QStringLiteral("{{CUSTOMIZE_TITLE}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Customize New Tab")));
    html.replace(QStringLiteral("{{CUSTOMIZE_BG_SECTION}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Background")));
    html.replace(QStringLiteral("{{CUSTOMIZE_NONE}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Solid color")));
    html.replace(QStringLiteral("{{CUSTOMIZE_RANDOM}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Shuffle")));
    html.replace(QStringLiteral("{{CUSTOMIZE_IMPORT}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Import image…")));
    html.replace(QStringLiteral("{{CUSTOMIZE_BLUR}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Blur effect")));
    html.replace(QStringLiteral("{{CUSTOMIZE_OPACITY}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Search bar transparency")));
    html.replace(QStringLiteral("{{CUSTOMIZE_SHOW_SITES}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Show Top Sites")));
    html.replace(QStringLiteral("{{CUSTOMIZE_RESET}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Reset to defaults")));
    html.replace(QStringLiteral("{{CUSTOMIZE_DONE}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage", "Done")));
    html.replace(QStringLiteral("{{IMPORT_UNSUPPORTED}}"),
                 NTP(QT_TRANSLATE_NOOP("NewTabPage",
                                       "This image could not be imported.")));
    html.replace(QStringLiteral("{{ENGINE_JSON}}"), engineJsonStr);
    html.replace(QStringLiteral("{{TOPSITES_JSON}}"), sitesJson);
    html.replace(QStringLiteral("{{SUGGEST_JSON}}"), suggestJson);
    html.replace(QStringLiteral("{{READONLY}}"), m_privateMode ? QStringLiteral("1")
                                                               : QStringLiteral("0"));
    return html;
}

void NewTabSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    const QUrl url = job->requestUrl();
    if (url.host() == QLatin1String("newtab")) {
        // Command URLs (nor://newtab/action/...) are handled and blocked by
        // WebPage::acceptNavigationRequest; reaching this handler means a
        // page tried to smuggle one in a subresource — refuse.
        if (url.path().startsWith(QLatin1String("/action/"))) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        // Restrict to GET to keep the internal page passive.
        if (job->requestMethod() != QByteArrayLiteral("GET")) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        auto *buffer = new QBuffer(job);
        buffer->setData(buildHtml().toUtf8());
        job->reply(QByteArrayLiteral("text/html; charset=utf-8"), buffer);
        return;
    }
    if (url.host() == QLatin1String("favicon") && m_favicons) {
        // nor://favicon/<site-host> -> cached PNG from the local store.
        if (job->requestMethod() != QByteArrayLiteral("GET")) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        const QString host = FaviconStore::sanitizeHost(url.path().mid(1));
        const QByteArray png = m_favicons->iconPng(host);
        if (host.isEmpty() || png.isEmpty()) {
            // No icon: the page falls back to the letter tile.
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        auto *buffer = new QBuffer(job);
        buffer->setData(png);
        job->reply(QByteArrayLiteral("image/png"), buffer);
        return;
    }
    if (url.host() == QLatin1String("background") && m_backgrounds) {
        // nor://background/<id>  -> full-size JPEG (bundled qrc or import).
        // nor://background/thumb/<id> -> 240x135 thumbnail for the gallery.
        if (job->requestMethod() != QByteArrayLiteral("GET")) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        QString path = url.path();
        bool thumb = false;
        if (path.startsWith(QLatin1String("/thumb/"))) {
            thumb = true;
            path = path.mid(QLatin1String("/thumb/").size());
        } else if (path.startsWith(QLatin1Char('/'))) {
            path = path.mid(1);
        }
        // Ids are strictly [a-z0-9-] (see BackgroundStore); the leading
        // "nor-"/"custom-" prefixes pin them to our own namespaces.
        const QByteArray bytes = thumb ? m_backgrounds->thumbnailBytes(path)
                                       : m_backgrounds->imageBytes(path);
        if (bytes.isEmpty()) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        auto *buffer = new QBuffer(job);
        buffer->setData(bytes);
        job->reply(QByteArrayLiteral("image/jpeg"), buffer);
        return;
    }
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
}
