// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "ProfileManager.h"

#include "AdBlockClient.h"
#include "AppConfig.h"
#include "BackgroundStore.h"
#include "DownloadManager.h"
#include "FaviconStore.h"
#include "HistoryStore.h"
#include "NewTabSchemeHandler.h"
#include "Paths.h"
#include "RequestInterceptor.h"
#include "SearchEngines.h"
#include "TopSitesStore.h"

#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QWebEngineSettings>

using namespace std::chrono_literals;

// The cookie-filter callback runs on WebEngine's IO thread and may run
// concurrently for several cookies; guard the policy snapshot with a lock
// belonging to the ProfileManager that owns the filter.
static QMutex g_cookieFilterMutex;

ProfileManager::ProfileManager(AppConfig &config, AdBlockClient &adBlock,
                               DownloadManager &downloads, HistoryStore *history,
                               SearchEngines *search, TopSitesStore *topSites,
                               FaviconStore *favicons, BackgroundStore *backgrounds,
                               QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_adBlock(adBlock)
    , m_downloads(downloads)
    , m_history(history)
    , m_search(search)
    , m_topSites(topSites)
    , m_favicons(favicons)
    , m_backgrounds(backgrounds)
{
    m_blockThirdPartyCookies = m_config.blockThirdPartyCookies();
}

QWebEngineProfile *ProfileManager::regularProfile()
{
    if (!m_regular)
        m_regular = createProfile(false);
    return m_regular;
}

QWebEngineProfile *ProfileManager::acquirePrivateProfile()
{
    if (!m_private)
        m_private = createProfile(true);
    return m_private;
}

void ProfileManager::privateSessionEnded()
{
    if (!m_private)
        return;
    QWebEngineProfile *profile = m_private;
    m_private = nullptr;
    // All private windows (and their pages) are already destroyed when
    // this is called; deleteLater() releases the profile and its
    // in-memory cookies/cache on the next event-loop iteration.
    profile->deleteLater();
}

QString ProfileManager::simplifiedUserAgentFor(const QString &ua)
{
    // Qt WebEngine advertises itself as
    //   ... (KHTML, like Gecko) QtWebEngine/6.8.2 Chrome/122.x.y.z Safari/537.36
    // The QtWebEngine token is the single most identifying bit of the
    // default agent (it narrows the population to Qt-based browsers).
    // Dropping it leaves a stock-Chromium agent that still truthfully
    // matches the underlying engine — unification, not spoofing.
    static const QRegularExpression token(
        QStringLiteral("\\s*QtWebEngine/\\d+(\\.\\d+)*"));
    QString out = ua;
    out.remove(token);
    return out.trimmed();
}

QWebEngineProfile *ProfileManager::createProfile(bool offRecord)
{
    QWebEngineProfile *profile = nullptr;
    if (offRecord) {
        // Default constructor => off-the-record: storage is in-memory and
        // is wiped when the profile is destroyed.
        profile = new QWebEngineProfile(this);
    } else {
        profile = new QWebEngineProfile(QStringLiteral("nor-browser"), this);
        const QString storage = Paths::webEngineDir() + QStringLiteral("/regular");
        profile->setPersistentStoragePath(storage);
        profile->setCachePath(storage + QStringLiteral("/cache"));
        profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
        profile->setPersistentCookiesPolicy(
            m_config.sessionOnlyCookies() ? QWebEngineProfile::NoPersistentCookies
                                          : QWebEngineProfile::AllowPersistentCookies);
    }
    profile->setSpellCheckEnabled(false);
    // Capture the stock engine User-Agent before any override is applied.
    m_defaultUserAgent = profile->httpUserAgent();
    installHooks(profile);
    applyWebAttributes(profile);
    return profile;
}

void ProfileManager::installHooks(QWebEngineProfile *profile)
{
    // Children of the profile: destroyed together with it, never after.
    auto *interceptor = new RequestInterceptor(m_config, m_adBlock, profile);
    profile->setUrlRequestInterceptor(interceptor);

    // The private (off-the-record) profile gets a read-only new tab: Top
    // Sites are displayed but pin/remove/edit are refused at the C++ layer
    // (WebPage::handleNewTabAction) — a private session never mutates the
    // user's real list.
    auto *handler = new NewTabSchemeHandler(m_history, m_search, m_topSites,
                                            m_favicons, &m_config, m_backgrounds,
                                            profile->isOffTheRecord(), profile);
    profile->installUrlSchemeHandler(QByteArrayLiteral("nor"), handler);

    connect(profile, &QWebEngineProfile::downloadRequested,
            &m_downloads, &DownloadManager::handleDownloadRequested);
}

void ProfileManager::installCookieFilter(QWebEngineProfile *profile)
{
    // Real third-party-cookie blocking: the filter rejects every cookie
    // whose origin is a different site than the first-party URL (Qt 6.8
    // QWebEngineCookieStore::setCookieFilter). Runs on the IO thread; the
    // policy flags are snapshotted under a mutex (QSettings is not
    // thread-safe) and refreshed by applyPrivacySettings().
    profile->cookieStore()->setCookieFilter(
        [this](const QWebEngineCookieStore::FilterRequest &request) {
            QMutexLocker locker(&g_cookieFilterMutex);
            if (!request.thirdParty)
                return true;
            if (m_blockThirdPartyCookies) {
                ++m_3pBlocked;
                return false;
            }
            ++m_3pAdmitted;
            return true;
        });
}

void ProfileManager::applyPrivacySettings(QWebEngineProfile *profile)
{
    if (!profile)
        return;

    // Snapshot the cookie policy for the IO-thread filter callback.
    {
        QMutexLocker locker(&g_cookieFilterMutex);
        m_blockThirdPartyCookies = m_config.blockThirdPartyCookies();
    }
    installCookieFilter(profile);

    // Cookie persistence (regular profile only; the private profile is
    // in-memory by construction).
    if (!profile->isOffTheRecord()) {
        profile->setPersistentCookiesPolicy(
            m_config.sessionOnlyCookies() ? QWebEngineProfile::NoPersistentCookies
                                          : QWebEngineProfile::AllowPersistentCookies);
    }

    // Unified User-Agent: drop the QtWebEngine token so every NØR user on
    // the same version presents the same stock-Chromium agent (matching
    // the real engine version). Can be disabled for troubleshooting.
    if (m_config.simplifiedUserAgent()) {
        const QString simplified = simplifiedUserAgentFor(m_defaultUserAgent);
        if (!simplified.isEmpty() && simplified != profile->httpUserAgent())
            profile->setHttpUserAgent(simplified);
    } else if (!m_defaultUserAgent.isEmpty()
               && profile->httpUserAgent() != m_defaultUserAgent) {
        profile->setHttpUserAgent(m_defaultUserAgent);
    }
}

void ProfileManager::applyWebAttributes(QWebEngineProfile *profile)
{
    QWebEngineSettings *settings = profile->settings();
    // Data Saver: images are blocked per network request in
    // RequestInterceptor (per-site exceptions work). The AutoLoadImages
    // profile attribute is intentionally NOT touched — it is global to the
    // profile and would ignore per-site exceptions (v1.0.0 limitation #3).
    settings->setAttribute(QWebEngineSettings::AutoLoadImages, true);
    // Privacy: hyperlink auditing (ping beacons) is always off.
    settings->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
    settings->setAttribute(QWebEngineSettings::ErrorPageEnabled, true);
    // Never run mixed (insecure) content inside secure pages.
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
    // Background autoplay burns CPU; media keeps requiring a gesture.
    settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, true);

    applyPrivacySettings(profile);

    // The request interceptor snapshots its policy (DNT/GPC, Data Saver)
    // for IO-thread use; refresh it whenever preferences change.
    const auto interceptors = profile->findChildren<RequestInterceptor *>();
    for (RequestInterceptor *interceptor : interceptors)
        interceptor->refreshPolicy();
}

void ProfileManager::applyWebAttributes()
{
    if (m_regular)
        applyWebAttributes(m_regular);
    if (m_private)
        applyWebAttributes(m_private);
}

void ProfileManager::clearCookiesNow()
{
    if (m_regular)
        m_regular->cookieStore()->deleteAllCookies();
    if (m_private)
        m_private->cookieStore()->deleteAllCookies();
}

void ProfileManager::clearHttpCacheNow()
{
    if (m_regular)
        m_regular->clearHttpCache();
    if (m_private)
        m_private->clearHttpCache();
}

void ProfileManager::clearVisitedLinksNow()
{
    if (m_regular)
        m_regular->clearAllVisitedLinks();
    if (m_private)
        m_private->clearAllVisitedLinks();
}

int ProfileManager::thirdPartyCookiesBlocked() const
{
    QMutexLocker locker(&g_cookieFilterMutex);
    return m_3pBlocked;
}

int ProfileManager::thirdPartyCookiesAdmitted() const
{
    QMutexLocker locker(&g_cookieFilterMutex);
    return m_3pAdmitted;
}

void ProfileManager::resetCookieFilterStats()
{
    QMutexLocker locker(&g_cookieFilterMutex);
    m_3pBlocked = 0;
    m_3pAdmitted = 0;
}
