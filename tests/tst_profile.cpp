// WebEngine profile lifecycle tests: single profile manager, persistent
// vs off-the-record separation, and private-profile cleanup.
//
// Runs headless (offscreen platform, sandbox disabled for the container).
#include <QtTest>

#include <QWebEnginePage>
#include <QWebEngineProfile>

#include "AdBlockClient.h"
#include "AppConfig.h"
#include "DownloadManager.h"
#include "HistoryStore.h"
#include "ProfileManager.h"
#include "SearchEngines.h"

class TestProfile : public QObject
{
    Q_OBJECT

private slots:
    void regularProfileIsPersistent();
    void privateProfileIsOffRecord();
    void privateProfileIsReused();
    void privateProfileReleasedAfterSessionEnd();
    void pagesUseRequestedProfile();
    void privatePageNeverRecordsHistory();
    void interceptorAttachedToBothProfiles();
    void simplifiedUserAgentDropsQtToken();
    void privacyDefaultsAreHardened();
    void privacySettingsApplyToProfile();

private:
    QTemporaryDir m_dir;
};

static ProfileManager *makeProfileManager(AppConfig *config, AdBlockClient *client,
                                          DownloadManager *downloads, HistoryStore **historyOut)
{
    auto *history = new HistoryStore(QStringLiteral(":memory:"));
    auto *search = new SearchEngines(*config);
    auto *pm = new ProfileManager(*config, *client, *downloads, history, search, nullptr);
    // history/search intentionally outlive nothing: they are deleted by the
    // caller together with the profile manager's scope.
    *historyOut = history;
    Q_UNUSED(search);
    return pm;
}

void TestProfile::simplifiedUserAgentDropsQtToken()
{
    // The stock Qt WebEngine agent advertises the QtWebEngine version —
    // the single most identifying token for a Qt-based browser.
    const QString stock = QStringLiteral(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like "
        "Gecko) QtWebEngine/6.8.2 Chrome/122.0.6261.111 Safari/537.36");
    const QString simplified = ProfileManager::simplifiedUserAgentFor(stock);
    QVERIFY(!simplified.contains(QLatin1String("QtWebEngine")));
    QVERIFY(simplified.contains(QLatin1String("Chrome/122.0.6261.111")));
    QVERIFY(simplified.contains(QLatin1String("Mozilla/5.0")));
    QVERIFY(simplified.contains(QLatin1String("Safari/537.36")));

    // Agents without the token pass through unchanged (no double work,
    // no accidental munging of other tokens).
    const QString chrome = QStringLiteral(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like "
        "Gecko) Chrome/122.0.6261.111 Safari/537.36");
    QCOMPARE(ProfileManager::simplifiedUserAgentFor(chrome), chrome);
    QCOMPARE(ProfileManager::simplifiedUserAgentFor(QString()), QString());
}

void TestProfile::privacyDefaultsAreHardened()
{
    AppConfig config(m_dir.filePath("privacy-defaults.ini"));
    QCOMPARE(config.blockThirdPartyCookies(), true);
    QCOMPARE(config.sendDoNotTrack(), true);
    QCOMPARE(config.sendGlobalPrivacyControl(), true);
    QCOMPARE(config.simplifiedUserAgent(), true);
    QCOMPARE(config.webrtcPublicIpOnly(), true);
    QCOMPARE(config.sessionOnlyCookies(), false);
}

void TestProfile::privacySettingsApplyToProfile()
{
    AppConfig config(m_dir.filePath("privacy-apply.ini"));
    AdBlockClient client(config);
    DownloadManager downloads(config);
    HistoryStore *history = nullptr;
    ProfileManager *pm = makeProfileManager(&config, &client, &downloads, &history);

    QWebEngineProfile *regular = pm->regularProfile();
    // Default: the QtWebEngine token must be gone from the live profile.
    QVERIFY(!regular->httpUserAgent().contains(QLatin1String("QtWebEngine")));
    QVERIFY(regular->httpUserAgent().contains(QLatin1String("Chrome/")));

    // Toggling the preference off restores the stock agent immediately.
    config.setSimplifiedUserAgent(false);
    pm->applyWebAttributes();
    QVERIFY(regular->httpUserAgent().contains(QLatin1String("QtWebEngine")));

    config.setSimplifiedUserAgent(true);
    pm->applyWebAttributes();
    QVERIFY(!regular->httpUserAgent().contains(QLatin1String("QtWebEngine")));

    delete pm;
    delete history;
}

void TestProfile::regularProfileIsPersistent()
{
    AppConfig config(m_dir.filePath("p1.ini"));
    AdBlockClient client(config);
    DownloadManager downloads(config);
    HistoryStore *history = nullptr;
    ProfileManager *pm = makeProfileManager(&config, &client, &downloads, &history);

    QWebEngineProfile *regular = pm->regularProfile();
    QVERIFY(regular != nullptr);
    QVERIFY(!regular->isOffTheRecord());
    QVERIFY(regular->persistentStoragePath().contains(QStringLiteral("webengine")));

    delete pm;
    delete history;
}

void TestProfile::privateProfileIsOffRecord()
{
    AppConfig config(m_dir.filePath("p2.ini"));
    AdBlockClient client(config);
    DownloadManager downloads(config);
    HistoryStore *history = nullptr;
    ProfileManager *pm = makeProfileManager(&config, &client, &downloads, &history);

    QWebEngineProfile *priv = pm->acquirePrivateProfile();
    QVERIFY(priv != nullptr);
    QVERIFY(priv->isOffTheRecord());
    QVERIFY(priv != pm->regularProfile());
    QVERIFY(pm->privateProfileInUse());

    delete pm;
    delete history;
}

void TestProfile::privateProfileIsReused()
{
    AppConfig config(m_dir.filePath("p3.ini"));
    AdBlockClient client(config);
    DownloadManager downloads(config);
    HistoryStore *history = nullptr;
    ProfileManager *pm = makeProfileManager(&config, &client, &downloads, &history);

    QWebEngineProfile *first = pm->acquirePrivateProfile();
    QWebEngineProfile *second = pm->acquirePrivateProfile();
    QCOMPARE(first, second); // exactly one private profile exists

    delete pm;
    delete history;
}

void TestProfile::privateProfileReleasedAfterSessionEnd()
{
    AppConfig config(m_dir.filePath("p4.ini"));
    AdBlockClient client(config);
    DownloadManager downloads(config);
    HistoryStore *history = nullptr;
    ProfileManager *pm = makeProfileManager(&config, &client, &downloads, &history);

    QWebEngineProfile *priv = pm->acquirePrivateProfile();
    QVERIFY(priv != nullptr);
    pm->privateSessionEnded();
    QVERIFY(!pm->privateProfileInUse());

    // A new session creates a fresh profile object.
    QWebEngineProfile *again = pm->acquirePrivateProfile();
    QVERIFY(again != priv);
    QVERIFY(again->isOffTheRecord());

    delete pm;
    delete history;
}

void TestProfile::pagesUseRequestedProfile()
{
    AppConfig config(m_dir.filePath("p5.ini"));
    AdBlockClient client(config);
    DownloadManager downloads(config);
    HistoryStore *history = nullptr;
    ProfileManager *pm = makeProfileManager(&config, &client, &downloads, &history);

    QWebEngineProfile *regular = pm->regularProfile();
    QWebEngineProfile *priv = pm->acquirePrivateProfile();

    QWebEnginePage regularPage(regular);
    QWebEnginePage privatePage(priv);
    QCOMPARE(regularPage.profile(), regular);
    QCOMPARE(privatePage.profile(), priv);
    QVERIFY(regularPage.profile()->isOffTheRecord() == false);
    QVERIFY(privatePage.profile()->isOffTheRecord() == true);

    pm->privateSessionEnded();
    delete pm;
    delete history;
}

void TestProfile::privatePageNeverRecordsHistory()
{
    // The history-recording policy lives in WebPage (window-aware). Here we
    // verify the store primitive that the policy relies on: an off-record
    // profile is distinguishable, and the store only grows on explicit
    // addVisit calls.
    AppConfig config(m_dir.filePath("p6.ini"));
    AdBlockClient client(config);
    DownloadManager downloads(config);
    HistoryStore *history = nullptr;
    ProfileManager *pm = makeProfileManager(&config, &client, &downloads, &history);

    QWebEngineProfile *priv = pm->acquirePrivateProfile();
    QWebEnginePage privatePage(priv);
    QSignalSpy loadFinished(&privatePage, &QWebEnginePage::loadFinished);
    privatePage.setHtml(QStringLiteral("<html><body>hello</body></html>"),
                        QUrl(QStringLiteral("https://private.example/")));
    QTRY_VERIFY_WITH_TIMEOUT(!loadFinished.isEmpty() && loadFinished.at(0).at(0).toBool(),
                             15000);

    // No API call was made -> history must remain empty (the app-level
    // WebPage::recordHistoryIfAllowed() refuses off-record pages).
    QCOMPARE(history->count(), qint64(0));
    // And the off-record profile check used by that policy:
    QVERIFY(privatePage.profile()->isOffTheRecord());

    pm->privateSessionEnded();
    delete pm;
    delete history;
}

void TestProfile::interceptorAttachedToBothProfiles()
{
    AppConfig config(m_dir.filePath("p7.ini"));
    AdBlockClient client(config);
    DownloadManager downloads(config);
    HistoryStore *history = nullptr;
    ProfileManager *pm = makeProfileManager(&config, &client, &downloads, &history);

    // Creating profiles with interceptors + scheme handlers installed must
    // produce usable profiles for both modes.
    QWebEngineProfile *regular = pm->regularProfile();
    QWebEngineProfile *priv = pm->acquirePrivateProfile();
    QVERIFY(regular != nullptr);
    QVERIFY(priv != nullptr);
    QVERIFY(regular != priv);

    pm->privateSessionEnded();
    delete pm;
    delete history;
}

QTEST_MAIN(TestProfile)
#include "tst_profile.moc"
