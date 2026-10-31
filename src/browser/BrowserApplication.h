#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>

class AdBlockClient;
class AppConfig;
class BackgroundStore;
class BookmarkStore;
class BrowserWindow;
class DownloadManager;
class FaviconStore;
class FilterListManager;
class HistoryStore;
class LanguageManager;
class ProfileManager;
class SearchEngines;
class TopSitesStore;

// Application orchestrator: owns every service and every window.
//
// Deletion order (critical for WebEngine safety):
//   1. all windows (which deletes all pages/views)
//   2. ProfileManager (which deletes profiles, interceptors, handlers)
//   3. remaining services as plain QObject children
class BrowserApplication : public QObject
{
    Q_OBJECT
public:
    explicit BrowserApplication(QObject *parent = nullptr);
    ~BrowserApplication() override;

    bool initialize(const QUrl &startUrl = QUrl(), bool privateWindow = false);

    // ---- smoke-test entry points (used by --smoke*, see main.cpp) -------
    int runSmokeNavigation(const QString &baseUrl, const QString &screenshotPath);
    int runSmokePrivate(const QString &baseUrl);
    int runSmokeHttps(const QUrl &url);
    int runSmokeLanguage(const QString &screenshotDir);
    int runSmokeDownload(const QString &baseUrl);
    int runSmokeCosmetic(const QString &baseUrl);
    int runSmokeTlsBad(const QUrl &tlsUrl, const QUrl &goodUrl);
    // End-to-end network blocking: real EasyList/EasyPrivacy + local rules,
    // engine-level decisions AND interceptor-level blocking on a live page.
    int runSmokeBlocking(const QString &baseUrl, const QString &easyListPath,
                         const QString &easyPrivacyPath);
    // Privacy hardening: DNT/GPC headers, unified User-Agent, third-party
    // cookie blocking (with first-party cookie control), live toggling.
    int runSmokePrivacy(const QString &baseUrl);
    // Top Sites end-to-end: new-user empty state, seeded history ranking,
    // pin/unpin/remove/edit through the real page, private-window
    // isolation, clear-history behaviour, viewport use (no status bar)
    // and tab-switch background. Saves screenshots into <dir>.
    int runSmokeTopSites(const QString &baseUrl, const QString &screenshotDir);
    // Chrome/appearance matrix at 1366x768: new tab, web page, Shorts-style
    // page, many tabs, tab context menu, settings/downloads/bookmarks
    // dialogs, fullscreen, AR/FR, resize, private window. Used to capture
    // comparable BEFORE/AFTER shots of UI redesigns. Neutral instrumentation.
    int runSmokeChrome(const QString &screenshotDir);
    // New Tab experience matrix: default background, light background,
    // busy-detail background, custom imported image, blur on/off, search
    // focus/typing, customization panel, AR/FR/EN, 1366x768. Saves
    // screenshots into <dir>.
    int runSmokeNewTab(const QString &screenshotDir);

    // ---- window management -------------------------------------------------
    BrowserWindow *createWindow(bool privateMode = false, const QUrl &startUrl = QUrl());
    QList<BrowserWindow *> windows() const;
    int privateWindowCount() const;

    // ---- service accessors ---------------------------------------------------
    AppConfig *config() const { return m_config; }
    LanguageManager *languages() const { return m_languages; }
    SearchEngines *searchEngines() const { return m_search; }
    BookmarkStore *bookmarks() const { return m_bookmarks; }
    HistoryStore *history() const { return m_history; }
    TopSitesStore *topSites() const { return m_topSites; }
    FaviconStore *favicons() const { return m_favicons; }
    BackgroundStore *backgrounds() const { return m_backgrounds; }
    DownloadManager *downloads() const { return m_downloads; }
    AdBlockClient *adBlock() const { return m_adBlock; }
    FilterListManager *filterLists() const { return m_filterLists; }
    ProfileManager *profiles() const { return m_profiles; }

    void retranslateUi();       // called after a language switch

private slots:
    void onWindowDestroyed();
    void onLanguageChanged(const QString &code);
    void onSettingsChanged();

private:
    void createServices();
    void connectServices();

    AppConfig *m_config = nullptr;
    LanguageManager *m_languages = nullptr;
    SearchEngines *m_search = nullptr;
    BookmarkStore *m_bookmarks = nullptr;
    HistoryStore *m_history = nullptr;
    TopSitesStore *m_topSites = nullptr;
    FaviconStore *m_favicons = nullptr;
    BackgroundStore *m_backgrounds = nullptr;
    DownloadManager *m_downloads = nullptr;
    AdBlockClient *m_adBlock = nullptr;
    FilterListManager *m_filterLists = nullptr;
    ProfileManager *m_profiles = nullptr; // manually deleted in ~dtor

    QList<QPointer<BrowserWindow>> m_windows;
};
