#pragma once

#include <QObject>

class QWebEngineProfile;
class AdBlockClient;
class AppConfig;
class BackgroundStore;
class DownloadManager;
class FaviconStore;
class HistoryStore;
class RequestInterceptor;
class NewTabSchemeHandler;
class SearchEngines;
class TopSitesStore;

// SINGLE owner of every QWebEngineProfile in the application.
//
// Lifetime contract (the class that previously caused crashes):
//  - The regular profile is created once and is persistent (named).
//  - The private profile is off-the-record and exists only while at least
//    one private window exists. It is destroyed with deleteLater() after
//    the last private window (and therefore all of its pages) is gone,
//    which wipes its in-memory storage.
//  - Interceptors and scheme handlers are QObject-children of their
//    profile, so they can never outlive it or dangle behind it.
//  - BrowserApplication deletes all windows BEFORE deleting the
//    ProfileManager, so profiles always die after their pages.
//
// Privacy hardening applied to every profile (see AppConfig):
//  - third-party cookies rejected through the cookie-store filter;
//  - optional session-only cookie persistence;
//  - optional unified Chromium User-Agent (no "QtWebEngine/x.y" token).
class ProfileManager : public QObject
{
    Q_OBJECT
public:
    ProfileManager(AppConfig &config, AdBlockClient &adBlock, DownloadManager &downloads,
                   HistoryStore *history, SearchEngines *search,
                   TopSitesStore *topSites = nullptr, FaviconStore *favicons = nullptr,
                   BackgroundStore *backgrounds = nullptr, QObject *parent = nullptr);

    QWebEngineProfile *regularProfile();
    // Returns the shared off-the-record profile, creating it on demand.
    QWebEngineProfile *acquirePrivateProfile();
    // Called by BrowserApplication when the last private window is gone.
    void privateSessionEnded();
    bool privateProfileInUse() const { return m_private != nullptr; }

    // Applies preference-driven settings (Data Saver, hyperlink auditing,
    // privacy hardening: cookies policy, User-Agent, request headers).
    void applyWebAttributes();

    // Live clearing of profile-owned data.
    void clearCookiesNow();
    void clearHttpCacheNow();
    void clearVisitedLinksNow();

    // Cookie-filter statistics (testable contract): how many third-party
    // cookies the filter rejected / admitted since the last reset.
    int thirdPartyCookiesBlocked() const;
    int thirdPartyCookiesAdmitted() const;
    void resetCookieFilterStats();

    // Removes the "QtWebEngine/x.y.z" token from a Qt WebEngine
    // User-Agent string, keeping the matching Chrome version so sites
    // still receive an agent that reflects the real engine capabilities.
    // Returns ua unchanged when the token is absent. Pure + unit-tested.
    static QString simplifiedUserAgentFor(const QString &ua);

private:
    QWebEngineProfile *createProfile(bool offRecord);
    void installHooks(QWebEngineProfile *profile);
    void applyWebAttributes(QWebEngineProfile *profile);
    void applyPrivacySettings(QWebEngineProfile *profile);
    void installCookieFilter(QWebEngineProfile *profile);

    AppConfig &m_config;
    AdBlockClient &m_adBlock;
    DownloadManager &m_downloads;
    HistoryStore *m_history;
    SearchEngines *m_search;
    TopSitesStore *m_topSites = nullptr;
    FaviconStore *m_favicons = nullptr;
    BackgroundStore *m_backgrounds = nullptr;

    // Snapshot of the cookie-filter policy consulted on WebEngine's IO
    // thread (QSettings must not be read there); updated under a mutex
    // shared by the cookie-filter callbacks of all profiles. The counters
    // record the filter's actual decisions (blocked/admitted third-party
    // cookies) so tests can assert on the component's behavior itself.
    bool m_blockThirdPartyCookies = true;
    int m_3pBlocked = 0;
    int m_3pAdmitted = 0;

    // The engine's stock User-Agent, captured at profile creation (before
    // any override) so the simplified/default toggle can restore it.
    QString m_defaultUserAgent;

    QWebEngineProfile *m_regular = nullptr;
    QWebEngineProfile *m_private = nullptr;
};
