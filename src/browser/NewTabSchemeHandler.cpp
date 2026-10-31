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
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QUrl>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

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
    QJsonArray sites;
    if (m_topSites) {
        const QVector<TopSite> top = m_topSites->topSites(kTopSitesLimit);
        for (const TopSite &s : top) {
            QJsonObject o;
            o.insert(QStringLiteral("url"), s.url);
            o.insert(QStringLiteral("title"), s.title);
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
                     (m_config ? m_config->newTabSearchOpacity() : 55) / 100.0);
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
