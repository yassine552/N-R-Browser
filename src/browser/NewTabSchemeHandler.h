#pragma once

#include <QObject>
#include <QUrl>

#include <QWebEngineUrlSchemeHandler>

class AppConfig;
class BackgroundStore;
class FaviconStore;
class HistoryStore;
class SearchEngines;
class TopSitesStore;

// True for any spelling of the internal New Tab page. Canonicalized URLs
// carry a trailing slash ("nor://newtab/"), QUrl-built ones may not —
// comparing scheme+host is the only robust check (a plain string compare
// used to leave the address bar showing "nor://newtab/").
inline bool isNewTabUrl(const QUrl &url)
{
    return url.scheme() == QLatin1String("nor") && url.host() == QLatin1String("newtab");
}

// Serves the custom "nor://newtab" page, "nor://favicon/<host>" icons and
// "nor://background/<id>" New-Tab wallpapers. The HTML is regenerated on
// every request, so language switching, RTL layout, the Top-Sites list and
// the background customization are always current after the page reloads.
//
// The page is self-contained (no remote subresource ever loads): the
// search engine, the Top Sites, the cached favicons, the customization
// preferences and the background gallery are injected server-side, and
// user actions travel back through nor://newtab/action/... URLs that
// WebPage::acceptNavigationRequest intercepts and blocks from actually
// navigating. Backgrounds are served from the local bundle / user import
// directory — the New Tab page NEVER loads anything from the network.
class NewTabSchemeHandler : public QWebEngineUrlSchemeHandler
{
public:
    // privateMode marks handlers installed on the off-the-record profile:
    // the new tab then renders Top Sites read-only (no pin/edit/remove), so
    // a private session can never mutate the user's real list.
    NewTabSchemeHandler(HistoryStore *history, SearchEngines *search,
                        TopSitesStore *topSites, FaviconStore *favicons,
                        AppConfig *config, BackgroundStore *backgrounds,
                        bool privateMode, QObject *parent = nullptr);
    // Compatibility constructor (unit tests): no Top Sites, not private.
    NewTabSchemeHandler(HistoryStore *history, SearchEngines *search,
                        QObject *parent = nullptr);

    void requestStarted(QWebEngineUrlRequestJob *job) override;

    // Registers the "nor" scheme. MUST be called before QApplication is
    // constructed (called from main()).
    static void registerNorScheme();

private:
    QString buildHtml() const;
    static QString escapeHtml(const QString &s);

    HistoryStore *m_history = nullptr;
    SearchEngines *m_search = nullptr;
    TopSitesStore *m_topSites = nullptr;
    FaviconStore *m_favicons = nullptr;
    AppConfig *m_config = nullptr;
    BackgroundStore *m_backgrounds = nullptr;
    bool m_privateMode = false;

    static constexpr int kTopSitesLimit = 12;
};
