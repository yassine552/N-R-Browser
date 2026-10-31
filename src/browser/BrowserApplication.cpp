#include "BrowserApplication.h"

#include "AdBlockClient.h"
#include "AddressBar.h"
#include "AppConfig.h"
#include "BackgroundStore.h"
#include "BookmarkStore.h"
#include "BrowserWindow.h"
#include "DownloadManager.h"
#include "FaviconStore.h"
#include "FilterListManager.h"
#include "FilterRule.h"
#include "HistoryStore.h"
#include "LanguageManager.h"
#include "Paths.h"
#include "ProfileManager.h"
#include "SearchEngines.h"
#include "TabWidget.h"
#include "TopSitesStore.h"
#include "WebPage.h"
#include "WebView.h"
#include "BookmarksDialog.h"
#include "DownloadsDialog.h"
#include "SettingsDialog.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QPixmap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMouseEvent>
#include <QStatusBar>
#include <QTest>
#include <QTabBar>
#include <QScreen>
#include <QTimer>
#include <QWebEngineScriptCollection>

#include "Theme.h"

using namespace std::chrono_literals;

namespace {
// Waits until a view reports loadFinished (or timeout).
bool waitForLoad(WebView *view, int timeoutMs)
{
    bool ok = false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(view, &QWebEngineView::loadFinished, &loop, [&](bool success) {
        ok = success;
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    return ok;
}

// Titles settle shortly AFTER loadFinished; wait for a non-empty title.
void waitForTitle(WebView *view, int timeoutMs)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (view->page()->title().isEmpty() && elapsed.elapsed() < timeoutMs)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
}

void smokeLine(const QString &name, bool pass, const QString &detail = QString())
{
    printf("SMOKE %-28s %s%s%s\n", name.toUtf8().constData(),
           pass ? "PASS" : "FAIL",
           detail.isEmpty() ? "" : "  ",
           detail.toUtf8().constData());
    fflush(stdout);
}
} // namespace

BrowserApplication::BrowserApplication(QObject *parent)
    : QObject(parent)
{
    createServices();
    connectServices();
}

BrowserApplication::~BrowserApplication()
{
    // 1) windows first: destroys all pages/views.
    for (QPointer<BrowserWindow> &w : m_windows) {
        if (w) {
            w->disconnect(this);
            delete w.data();
        }
    }
    m_windows.clear();
    // 2) profiles next: interceptors and scheme handlers die with them.
    delete m_profiles;
    m_profiles = nullptr;
    // 3) remaining services are QObject children of this object and are
    //    deleted automatically afterwards.
}

void BrowserApplication::createServices()
{
    m_config = new AppConfig(this);
    m_languages = new LanguageManager(*m_config, this);
    m_search = new SearchEngines(*m_config, this);
    m_bookmarks = new BookmarkStore(Paths::bookmarksDbPath(), this);
    m_history = new HistoryStore(Paths::historyDbPath(), this);
    m_topSites = new TopSitesStore(m_history, Paths::topSitesStatePath(), this);
    m_favicons = new FaviconStore(Paths::faviconsDir(), this);
    m_backgrounds = new BackgroundStore(Paths::backgroundsDir(), this);
    m_downloads = new DownloadManager(*m_config, this);
    m_adBlock = new AdBlockClient(*m_config, this);
    m_filterLists = new FilterListManager(*m_config, *m_adBlock, this);
    // Cached lists + serialized engine when inputs are unchanged;
    // otherwise parse from the list files (see FilterListManager).
    m_filterLists->initialize();
    m_profiles = new ProfileManager(*m_config, *m_adBlock, *m_downloads, m_history, m_search,
                                    m_topSites, m_favicons, m_backgrounds,
                                    nullptr /* deleted manually, see dtor */);
}

void BrowserApplication::connectServices()
{
    connect(m_config, &AppConfig::changed, this, &BrowserApplication::onSettingsChanged);
    connect(m_languages, &LanguageManager::languageChanged,
            this, &BrowserApplication::onLanguageChanged);
    // Whenever the engine is rebuilt (settings dialog reload, filter-list
    // update), refresh the serialized startup cache.
    connect(m_adBlock, &AdBlockClient::rulesChanged,
            m_filterLists, &FilterListManager::saveEngineCache);
}

bool BrowserApplication::initialize(const QUrl &startUrl, bool privateWindow)
{
    m_languages->initialize();
    BrowserWindow *window = createWindow(privateWindow, startUrl);
    if (!window)
        return false;
    // Kick off filter-list updates shortly after the first window is up:
    // downloads are asynchronous and never block startup; when they land
    // the engine is rebuilt in the background.
    QTimer::singleShot(1500, this, [this]() { m_filterLists->updateAll(); });
    return true;
}

BrowserWindow *BrowserApplication::createWindow(bool privateMode, const QUrl &startUrl)
{
    QWebEngineProfile *profile = privateMode ? m_profiles->acquirePrivateProfile()
                                             : m_profiles->regularProfile();
    if (!profile)
        return nullptr;
    BrowserWindow *window = new BrowserWindow(this, privateMode);
    m_windows.append(window);
    window->newInitialTab(startUrl);
    window->show();

    // Lifetime tracking via the window's own destructor signal.
    connect(window, &BrowserWindow::windowGone, this,
            [this, window, privateMode](bool wasPrivate) {
                m_windows.removeAll(window);
                if (wasPrivate && privateWindowCount() == 0)
                    m_profiles->privateSessionEnded();
                if (m_windows.isEmpty())
                    QApplication::quit();
            });
    return window;
}

QList<BrowserWindow *> BrowserApplication::windows() const
{
    QList<BrowserWindow *> result;
    for (const QPointer<BrowserWindow> &w : m_windows)
        if (w)
            result.append(w.data());
    return result;
}

int BrowserApplication::privateWindowCount() const
{
    int n = 0;
    for (const QPointer<BrowserWindow> &w : m_windows)
        if (w && w->isPrivate())
            ++n;
    return n;
}

void BrowserApplication::onWindowDestroyed()
{
    m_windows.removeAll(nullptr);
}

void BrowserApplication::onLanguageChanged(const QString &code)
{
    Q_UNUSED(code);
    retranslateUi();
}

void BrowserApplication::onSettingsChanged()
{
    // Engine toggle / allowlist snapshot (thread-safe for the IO thread).
    m_adBlock->refreshConfigCache();
    m_profiles->applyWebAttributes();
}

void BrowserApplication::retranslateUi()
{
    for (const QPointer<BrowserWindow> &w : m_windows)
        if (w)
            w->retranslateUi();
}

// ---------------------------------------------------------------- smoke ----

int BrowserApplication::runSmokeNavigation(const QString &baseUrl, const QString &screenshotPath)
{
    int pass = 0, fail = 0;
    BrowserWindow *window = createWindow(false, QUrl(baseUrl + QStringLiteral("/hello")));
    WebView *view = window->currentView();

    const bool first = waitForLoad(view, 20000);
    waitForTitle(view, 5000);
    const QString helloTitle = view->page()->title();
    const bool titleOk = helloTitle.contains(QStringLiteral("NOR Smoke"));
    smokeLine("load/hello", first && titleOk,
              QStringLiteral("title='%1' loadOk=%2").arg(helloTitle).arg(first));
    first && titleOk ? ++pass : ++fail;

    // Second tab with a link page.
    WebPage *page2 = window->openInNewForegroundTab(QUrl(baseUrl + QStringLiteral("/links")));
    WebView *view2 = window->currentView();
    const bool second = waitForLoad(view2, 20000);
    waitForTitle(view2, 5000);
    smokeLine("multi-tab", second && window->tabCount() == 2,
              QStringLiteral("tabs=%1 loadOk=%2 title='%3'")
                  .arg(window->tabCount())
                  .arg(second)
                  .arg(view2->page()->title()));
    second && window->tabCount() == 2 ? ++pass : ++fail;

    // Navigation: forward then back.
    view2->load(QUrl(baseUrl + QStringLiteral("/target")));
    waitForLoad(view2, 20000);
    const QString afterTarget = view2->page()->url().path();
    view2->back();
    waitForLoad(view2, 20000);
    const bool backOk = view2->page()->url().path() == QStringLiteral("/links");
    view2->forward();
    waitForLoad(view2, 20000);
    const bool fwdOk = view2->page()->url().path() == QStringLiteral("/target")
                       && afterTarget == QStringLiteral("/target");
    smokeLine("history/back+forward", backOk && fwdOk);
    backOk && fwdOk ? ++pass : ++fail;

    // Address bar semantics (URL vs search) without navigating.
    const SearchEngine engine = m_search->defaultEngine();
    const bool urlDetect = SearchEngines::looksLikeUrl(QStringLiteral("example.org/page"));
    const bool queryDetect = !SearchEngines::looksLikeUrl(QStringLiteral("nor browser test"));
    const bool intlQuery = QString::fromUtf8(
                                 SearchEngines::urlForQuery(QStringLiteral("متصفح خصوصي"), engine)
                                     .toEncoded())
                                 .contains(QStringLiteral("%D9%85"));
    smokeLine("addressbar/url-vs-search", urlDetect && queryDetect && intlQuery);
    urlDetect && queryDetect && intlQuery ? ++pass : ++fail;

    if (!screenshotPath.isEmpty()) {
        QDir().mkpath(QFileInfo(screenshotPath).absolutePath());
        const bool saved = window->grab().save(screenshotPath);
        smokeLine("screenshot", saved, screenshotPath);
        saved ? ++pass : ++fail;
    }
    Q_UNUSED(page2);

    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

int BrowserApplication::runSmokePrivate(const QString &baseUrl)
{
    int pass = 0, fail = 0;
    const qint64 historyBefore = m_history->count();

    BrowserWindow *window = createWindow(true, QUrl(baseUrl + QStringLiteral("/hello")));
    WebView *view = window->currentView();
    const bool loaded = waitForLoad(view, 20000);

    QWebEngineProfile *privateProfile = view->page()->profile();
    QWebEngineProfile *regularProfile = m_profiles->regularProfile();
    const bool offRecord = privateProfile->isOffTheRecord();
    const bool regularOk = regularProfile && !regularProfile->isOffTheRecord()
                           && regularProfile != privateProfile;
    smokeLine("private/off-the-record", loaded && offRecord && regularOk);
    loaded && offRecord && regularOk ? ++pass : ++fail;

    // The private page must not land in the history database.
    const qint64 historyAfterLoad = m_history->count();
    smokeLine("private/no-history", historyAfterLoad == historyBefore);
    historyAfterLoad == historyBefore ? ++pass : ++fail;

    // Closing the private session must release the off-record profile.
    window->close(); // saves geometry, hides
    window->deleteLater();
    // Process the deferred deletion deterministically (this also triggers
    // the destroyed() hook that releases the private profile).
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents(QEventLoop::AllEvents, 200);
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const bool released = !m_profiles->privateProfileInUse();
    smokeLine("private/profile-released", released,
              QStringLiteral("windows=%1 stillInUse=%2")
                  .arg(m_windows.size())
                  .arg(m_profiles->privateProfileInUse()));
    released ? ++pass : ++fail;

    const qint64 historyAfterClose = m_history->count();
    smokeLine("private/history-unchanged", historyAfterClose == historyBefore);
    historyAfterClose == historyBefore ? ++pass : ++fail;

    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

int BrowserApplication::runSmokeHttps(const QUrl &url)
{
    BrowserWindow *window = createWindow(false, url);
    WebView *view = window->currentView();
    const bool loaded = waitForLoad(view, 30000);
    const QString title = view->page()->title();
    if (loaded) {
        smokeLine("https/load", true,
                  QStringLiteral("title='%1' url=%2").arg(title, view->page()->url().toString()));
        printf("SMOKE RESULT pass=1 fail=0\n");
        fflush(stdout);
        return 0;
    }
    // Distinguish network failure from application failure.
    smokeLine("https/load", false,
              QStringLiteral("network failure or blocked access (title='%1')").arg(title));
    printf("SMOKE RESULT pass=0 fail=1 NETWORK\n");
    fflush(stdout);
    return 2;
}

int BrowserApplication::runSmokeDownload(const QString &baseUrl)
{
    int pass = 0, fail = 0;
    // Isolated destination, auto-accept (no file dialog under Xvfb).
    const QString dlDir = Paths::appDataDir() + QStringLiteral("/smoke-downloads");
    QDir(dlDir).removeRecursively();
    QDir().mkpath(dlDir);
    m_config->setAskWhereToSaveDownloads(false);
    m_config->setDownloadDirectory(dlDir);

    // Chromium does not start downloads without a genuine user gesture
    // (JS .click() does not count), so the smoke test loads the links page
    // and sends real mouse events on the download link.
    BrowserWindow *window = createWindow(false, QUrl(baseUrl + QStringLiteral("/links")));
    WebView *view = window->currentView();
    const bool loaded = waitForLoad(view, 20000);
    qInfo("smoke-download: pageLoaded=%d", loaded);

    // Ask the page where the link is (async callback stores the center).
    QPointF linkCenter;
    view->page()->runJavaScript(
        QStringLiteral("(function(){try{var e=document.getElementById('dl');"
                       "if(!e) return document.title + ' | no-dl';"
                       "var r=e.getBoundingClientRect();"
                       "return [r.x+r.width/2,r.y+r.height/2];}"
                       "catch(err){return 'ERR ' + err.message;}})()"),
        [&linkCenter](const QVariant &v) {
            qInfo("smoke-download: js result: %s", qPrintable(v.toString()));
            const QVariantList list = v.toList();
            if (list.size() == 2 && list.at(0).toReal() >= 0)
                linkCenter = QPointF(list.at(0).toReal(), list.at(1).toReal());
        });
    QElapsedTimer coordWait;
    coordWait.start();
    while (linkCenter.isNull() && coordWait.elapsed() < 5000)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    qInfo("smoke-download: linkCenter=%f,%f", linkCenter.x(), linkCenter.y());

    if (!linkCenter.isNull()) {
        window->activateWindow();
        window->raise();
        view->setFocus();
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        const QPoint pos(qRound(linkCenter.x()), qRound(linkCenter.y()));
        // QTest generates events with valid timestamps; Chromium's gesture
        // recognizer needs them to treat this as a real user click. The
        // actual event target is WebEngine's internal delegate child, which
        // is created asynchronously after the first rendered frame — under
        // software rendering (Xvfb) it can appear well after loadFinished.
        // Clicking the parent view instead loses the gesture, so wait for
        // the delegate child (bounded) before falling back.
        qInfo("smoke-download: windowGeom=%dx%d viewGeom=%dx%d",
              window->width(), window->height(), view->width(), view->height());
        QWidget *target = nullptr;
        QElapsedTimer delegateWait;
        delegateWait.start();
        while (delegateWait.elapsed() < 8000) {
            QWidget *child = view->childAt(pos);
            if (child && child != view) {
                target = child;
                break;
            }
            QApplication::processEvents(QEventLoop::AllEvents, 25);
        }
        if (!target)
            target = view;
        qInfo("smoke-download: delegate ready after %lld ms",
              (long long)delegateWait.elapsed());
        qInfo("smoke-download: clicking %s at view(%d,%d)",
              target->metaObject()->className(), pos.x(), pos.y());
        QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, target->mapFrom(view, pos));
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        qInfo("smoke-download: clicked urlAfter=%s",
              qPrintable(view->page()->url().toString()));
    }

    // Wait for the download to appear and finish.
    QElapsedTimer elapsed;
    elapsed.start();
    DownloadItem *item = nullptr;
    while (elapsed.elapsed() < 30000) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!m_downloads->items().isEmpty()) {
            item = m_downloads->items().first();
            if (item->state() == DownloadState::Completed
                || item->state() == DownloadState::Cancelled
                || item->state() == DownloadState::Interrupted)
                break;
        }
    }

    const bool hasItem = item != nullptr;
    smokeLine("download/started", hasItem);
    hasItem ? ++pass : ++fail;

    if (item) {
        const bool completed = item->state() == DownloadState::Completed;
        smokeLine("download/completed", completed,
                  QStringLiteral("state=%1").arg(int(item->state())));
        completed ? ++pass : ++fail;

        QFile file(item->filePath());
        const bool contentOk = completed && file.exists()
                               && file.open(QIODevice::ReadOnly)
                               && file.readAll().contains("NOR smoke download payload");
        smokeLine("download/content", contentOk, item->filePath());
        contentOk ? ++pass : ++fail;

        // A second file with the same name must not overwrite: the helper
        // must produce a numbered variant when the first file exists.
        const QString unique = DownloadManager::uniqueFileName(
            dlDir, QStringLiteral("nor-smoke-file.txt"));
        const bool uniqueName = (unique == QStringLiteral("nor-smoke-file.txt"))
                                    ? !QFileInfo::exists(QDir(dlDir).filePath(unique))
                                    : true;
        smokeLine("download/unique-names", uniqueName, unique);
        uniqueName ? ++pass : ++fail;
    }

    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

int BrowserApplication::runSmokeCosmetic(const QString &baseUrl)
{
    int pass = 0, fail = 0;

    // Cosmetic rules for the test host: a URL-specific hide, a generic
    // hide, and an exception that must keep .content visible.
    const QUrl pageUrl(baseUrl + QStringLiteral("/cosmetic"));
    const QString listPath =
        Paths::appDataDir() + QStringLiteral("/smoke-cosmetic-list.txt");
    {
        QFile f(listPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            printf("SMOKE RESULT pass=0 fail=1 (cannot write cosmetic list)\n");
            fflush(stdout);
            return 1;
        }
        f.write("127.0.0.1##.ad-banner\n"
                "##.generic-ad\n"
                "127.0.0.1##.content-would-be-hidden\n");
    }
    const QStringList previousLists = m_config->customFilterLists();
    m_config->setCustomFilterLists(QStringList() << listPath);
    m_adBlock->reload();
    m_config->setCustomFilterLists(previousLists); // restored below anyway

    if (!m_adBlock->cosmeticFilteringSupported()) {
        smokeLine("cosmetic/supported", false,
                  QStringLiteral("backend '%1' has no cosmetic support")
                      .arg(m_adBlock->backendName()));
        printf("SMOKE RESULT pass=0 fail=1\n");
        fflush(stdout);
        return 1;
    }
    smokeLine("cosmetic/supported", true,
              QStringLiteral("backend=%1").arg(m_adBlock->backendName()));
    ++pass;

    BrowserWindow *window = createWindow(false, pageUrl);
    WebView *view = window->currentView();
    const bool loaded = waitForLoad(view, 20000);
    // The generic pass runs after loadFinished and is asynchronous.
    QTest::qWait(600);
    QApplication::processEvents(QEventLoop::AllEvents, 100);

    // The URL-specific script must be registered on the page.
    const bool scriptRegistered =
        !view->page()->scripts()
             .find(QStringLiteral("nor-cosmetic-filters"))
             .isEmpty();
    smokeLine("cosmetic/script-registered", loaded && scriptRegistered);
    (loaded && scriptRegistered) ? ++pass : ++fail;

    // Ask the page what is actually hidden.
    QString json;
    view->page()->runJavaScript(
        QStringLiteral(
            "(function(){function d(s){var e=document.querySelector(s);"
            "return e?getComputedStyle(e).display:'missing';}"
            "return JSON.stringify({banner:d('.ad-banner'),"
            "generic:d('.generic-ad'),content:d('.content')});})()"),
        [&json](const QVariant &v) { json = v.toString(); });
    QElapsedTimer jsWait;
    jsWait.start();
    while (json.isEmpty() && jsWait.elapsed() < 5000)
        QApplication::processEvents(QEventLoop::AllEvents, 20);

    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    const QJsonObject obj = doc.object();
    const QString banner = obj.value(QLatin1String("banner")).toString();
    const QString generic = obj.value(QLatin1String("generic")).toString();
    const QString content = obj.value(QLatin1String("content")).toString();

    const bool bannerHidden = banner == QLatin1String("none");
    smokeLine("cosmetic/specific-hidden", bannerHidden,
              QStringLiteral("display=%1").arg(banner));
    bannerHidden ? ++pass : ++fail;

    const bool genericHidden = generic == QLatin1String("none");
    smokeLine("cosmetic/generic-hidden", genericHidden,
              QStringLiteral("display=%1").arg(generic));
    genericHidden ? ++pass : ++fail;

    const bool contentVisible = content != QLatin1String("none")
                                && content != QLatin1String("missing");
    smokeLine("cosmetic/content-visible", contentVisible,
              QStringLiteral("display=%1").arg(content));
    contentVisible ? ++pass : ++fail;

    // Clean up: remove the smoke list from the engine.
    m_config->setCustomFilterLists(previousLists);
    m_adBlock->reload();

    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

int BrowserApplication::runSmokeBlocking(const QString &baseUrl,
                                         const QString &easyListPath,
                                         const QString &easyPrivacyPath)
{
    int pass = 0, fail = 0;

    // --- Load the real EasyList + EasyPrivacy plus deterministic local rules.
    const QString localRulesPath =
        Paths::appDataDir() + QStringLiteral("/smoke-blocking-rules.txt");
    {
        QFile f(localRulesPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            printf("SMOKE RESULT pass=0 fail=1 (cannot write local rules)\n");
            fflush(stdout);
            return 1;
        }
        f.write("! NØR blocking smoke rules\n"
                "blocked-ad.js$script\n"
                "allowed-exception.js$script\n"
                "@@allowed-exception.js$script\n");
    }
    QStringList lists;
    lists << localRulesPath;
    int realRules = 0;
    for (const QString &p : { easyListPath, easyPrivacyPath }) {
        if (!p.isEmpty() && QFile::exists(p)) {
            lists << p;
            ++realRules;
        }
    }

    const QStringList previousLists = m_config->customFilterLists();
    m_config->setCustomFilterLists(lists);
    m_adBlock->reload();
    m_adBlock->resetBlockedTotal();

    smokeLine("blocking/rules-loaded", realRules == 2,
              QStringLiteral("lists=%1 rules=%2")
                  .arg(realRules)
                  .arg(m_adBlock->blockRuleCount()));
    (realRules == 2) ? ++pass : ++fail;

    // --- Engine-level decisions against the REAL EasyList/EasyPrivacy -----
    const QUrl pageUrl(baseUrl + QStringLiteral("/blocking"));
    struct TrackerCheck { const char *name; const char *url; bool shouldBlock; };
    const TrackerCheck checks[] = {
        { "doubleclick-gpt", "https://securepubads.g.doubleclick.net/tag/js/gpt.js", true },
        { "google-analytics", "https://www.google-analytics.com/analytics.js", true },
        { "googletagmanager", "https://www.googletagmanager.com/gtag/js?id=GTM-NORTEST", true },
        { "scorecardresearch", "https://sb.scorecardresearch.com/beacon.js", true },
        // Benign first-party-ish resources must NOT be blocked.
        { "example-com-page", "https://example.com/index.html", false },
    };
    for (const TrackerCheck &c : checks) {
        const FilterDecision d = m_adBlock->decide(QUrl(QString::fromLatin1(c.url)),
                                                   pageUrl, FilterRule::TypeScript);
        const bool ok = (d.block == c.shouldBlock);
        smokeLine(QStringLiteral("blocking/engine-%1").arg(QLatin1String(c.name)), ok,
                  QStringLiteral("block=%1 (want %2) rules=%3")
                      .arg(d.block)
                      .arg(c.shouldBlock)
                      .arg(m_adBlock->blockRuleCount()));
        ok ? ++pass : ++fail;
    }

    // --- Live page: prove the interceptor blocks on the IO thread ----------
    BrowserWindow *window = createWindow(false, pageUrl);
    WebView *view = window->currentView();
    const bool loaded = waitForLoad(view, 20000);
    // Sub-resources settle after loadFinished; allow time for the trackers.
    QTest::qWait(2500);
    QApplication::processEvents(QEventLoop::AllEvents, 200);

    QString json;
    view->page()->runJavaScript(
        QStringLiteral("(function(){try{return JSON.stringify(window.__nor||{});}"
                       "catch(e){return '{}';}})()"),
        [&json](const QVariant &v) { json = v.toString(); });
    QElapsedTimer jsWait;
    jsWait.start();
    while (json.isEmpty() && jsWait.elapsed() < 5000)
        QApplication::processEvents(QEventLoop::AllEvents, 20);

    const QJsonObject results =
        QJsonDocument::fromJson(json.toUtf8()).object();
    auto outcome = [&results](const char *key) {
        return results.value(QLatin1String(key)).toString();
    };
    auto check = [&](const char *key, bool wantLoad) {
        const QString got = outcome(key);
        // A blocked request fires onerror; an allowed one fires onload.
        // 'pending' means the request never finished (ambiguous → fail).
        const bool ok = loaded && ((wantLoad && got == QLatin1String("load"))
                                   || (!wantLoad && got == QLatin1String("error")));
        smokeLine(QStringLiteral("blocking/page-%1").arg(QLatin1String(key)), ok,
                  QStringLiteral("result=%1 want=%2").arg(got, wantLoad ? "load" : "error"));
        ok ? ++pass : ++fail;
    };
    check("img-local", true);          // normal images still work
    check("script-local", true);       // normal scripts still work
    check("local-blocked", false);     // custom blocking rule applied
    check("local-exception", true);    // @@ exception rule wins
    check("doubleclick", false);       // real EasyList rule
    check("ganalytics", false);        // real EasyList rule
    check("gtm", false);               // real EasyPrivacy rule
    check("scorecard", false);         // real EasyPrivacy rule

    // The session counter proves the interceptor really ran (it is only
    // incremented from interceptRequest on WebEngine's IO thread).
    const int blockedTotal = m_adBlock->blockedTotal();
    const bool interceptorRan = loaded && blockedTotal >= 4;
    smokeLine("blocking/interceptor-ran", interceptorRan,
              QStringLiteral("blockedTotal=%1 (need >=4)").arg(blockedTotal));
    interceptorRan ? ++pass : ++fail;

    // --- Restore ------------------------------------------------------------
    m_config->setCustomFilterLists(previousLists);
    m_adBlock->reload();
    m_adBlock->resetBlockedTotal();

    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

int BrowserApplication::runSmokePrivacy(const QString &baseUrl)
{
    int pass = 0, fail = 0;

    // --- Phase 1: defaults (hardening ON) -----------------------------------
    m_config->setBlockThirdPartyCookies(true);
    m_config->setSendDoNotTrack(true);
    m_config->setSendGlobalPrivacyControl(true);
    m_config->setSimplifiedUserAgent(true);
    QApplication::processEvents(QEventLoop::AllEvents, 100);

    BrowserWindow *window = createWindow(false, QUrl(baseUrl + QStringLiteral("/headers")));
    WebView *view = window->currentView();
    // Let the initial document settle first (waitForLoad must not miss the
    // loadFinished signal while we process events below).
    waitForLoad(view, 20000);
    // A leftover cookie from an earlier run would falsify the cookie checks
    // below; clear the (now existing) profile's jar and reload.
    m_profiles->clearCookiesNow();
    m_profiles->resetCookieFilterStats();
    QApplication::processEvents(QEventLoop::AllEvents, 300);
    view->load(QUrl(baseUrl + QStringLiteral("/headers")));
    const bool loaded = waitForLoad(view, 20000);

    // Request headers as the SERVER received them for the main document.
    auto headerJson = [&view](QString &out) {
        out.clear();
        view->page()->runJavaScript(
            QStringLiteral("document.getElementById('h').textContent"),
            [&out](const QVariant &v) { out = v.toString(); });
        QElapsedTimer wait;
        wait.start();
        while (out.isEmpty() && wait.elapsed() < 5000)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
    };
    QString json;
    headerJson(json);
    const QJsonObject headers =
        QJsonDocument::fromJson(json.toUtf8()).object();
    auto header = [&headers](const char *name) {
        return headers.value(QLatin1String(name)).toString();
    };

    const QString dnt = header("dnt");
    const bool dntOk = loaded && dnt == QLatin1String("1");
    smokeLine("privacy/dnt-header", dntOk, QStringLiteral("DNT=%1").arg(dnt));
    dntOk ? ++pass : ++fail;

    const QString gpc = header("sec-gpc");
    const bool gpcOk = loaded && gpc == QLatin1String("1");
    smokeLine("privacy/gpc-header", gpcOk, QStringLiteral("Sec-GPC=%1").arg(gpc));
    gpcOk ? ++pass : ++fail;

    const QString ua = header("user-agent");
    const bool uaOk = loaded && !ua.contains(QLatin1String("QtWebEngine"))
                      && ua.contains(QLatin1String("Chrome/"));
    smokeLine("privacy/unified-user-agent", uaOk, ua);
    uaOk ? ++pass : ++fail;

    // First-party cookies must keep working while 3p-blocking is on.
    view->load(QUrl(baseUrl + QStringLiteral("/fp-cookie")));
    const bool fpLoaded = waitForLoad(view, 20000);
    QTest::qWait(400);
    QString fp;
    view->page()->runJavaScript(QStringLiteral("window.__fp||'missing'"),
                                [&fp](const QVariant &v) { fp = v.toString(); });
    QElapsedTimer fpWait;
    fpWait.start();
    while (fp.isEmpty() && fpWait.elapsed() < 4000)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    const bool fpOk = fpLoaded && fp == QLatin1String("ok");
    smokeLine("privacy/first-party-cookie-works", fpOk,
              QStringLiteral("result=%1").arg(fp));
    fpOk ? ++pass : ++fail;

    // Third-party cookie must be REJECTED: a cross-site iframe
    // (localhost page embedding https://127.0.0.1) must not get its cookie
    // through, and the filter must have recorded the rejection itself.
    QString localBaseUrl = baseUrl;
    localBaseUrl.replace(QLatin1String("127.0.0.1"), QLatin1String("localhost"));
    m_profiles->resetCookieFilterStats();
    view->load(QUrl(localBaseUrl + QStringLiteral("/privacy")));
    const bool tpLoaded = waitForLoad(view, 20000);
    QString tp;
    {
        QElapsedTimer tpWait;
        tpWait.start();
        while (tpWait.elapsed() < 8000) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            QString probe;
            view->page()->runJavaScript(
                QStringLiteral("window.__nor&&window.__nor.tp?window.__nor.tp:''"),
                [&probe](const QVariant &v) { probe = v.toString(); });
            QElapsedTimer inner;
            inner.start();
            while (probe.isEmpty() && inner.elapsed() < 1000)
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            if (!probe.isEmpty()) {
                tp = probe;
                break;
            }
        }
    }
    const int blocked3p = m_profiles->thirdPartyCookiesBlocked();
    const bool tpBlocked = tpLoaded && tp == QLatin1String("blocked")
                           && blocked3p >= 1;
    smokeLine("privacy/third-party-cookie-blocked", tpBlocked,
              QStringLiteral("iframe reports: %1; filter rejected %2 third-party cookie(s)")
                  .arg(tp.isEmpty() ? QStringLiteral("<no message>") : tp)
                  .arg(blocked3p));
    tpBlocked ? ++pass : ++fail;

    // --- Phase 2: hardening OFF must take effect without a restart ----------
    m_config->setBlockThirdPartyCookies(false);
    m_config->setSendDoNotTrack(false);
    m_config->setSendGlobalPrivacyControl(false);
    m_config->setSimplifiedUserAgent(false);
    QApplication::processEvents(QEventLoop::AllEvents, 200);

    view->load(QUrl(baseUrl + QStringLiteral("/headers")));
    const bool loaded2 = waitForLoad(view, 20000);
    QString json2;
    headerJson(json2);
    const QJsonObject headers2 =
        QJsonDocument::fromJson(json2.toUtf8()).object();
    const bool offOk = loaded2 && !headers2.contains(QLatin1String("dnt"))
                       && !headers2.contains(QLatin1String("sec-gpc"))
                       && headers2.value(QLatin1String("user-agent")).toString()
                              .contains(QLatin1String("QtWebEngine"));
    smokeLine("privacy/signals-off-when-disabled", offOk);
    offOk ? ++pass : ++fail;

    // Control: with the setting off, the SAME third-party cookie must be
    // ADMITTED by the filter. We assert on the filter's own decision
    // (recorded by ProfileManager) rather than on the cookie ending up in
    // document.cookie: over a self-signed local TLS endpoint Chromium
    // itself drops cross-site cookies regardless, so the page-level signal
    // cannot distinguish NØR's filter from Chromium's cookie policy here.
    // The filter decision is the browser component's contract.
    m_profiles->resetCookieFilterStats();
    view->load(QUrl(localBaseUrl + QStringLiteral("/privacy")));
    const bool tpLoaded2 = waitForLoad(view, 20000);
    QTest::qWait(1500); // give the iframe time to attempt its cookie
    QApplication::processEvents(QEventLoop::AllEvents, 200);
    const int admitted3p = m_profiles->thirdPartyCookiesAdmitted();
    const bool tpAdmitted = tpLoaded2 && admitted3p >= 1;
    smokeLine("privacy/third-party-cookie-admitted-when-disabled", tpAdmitted,
              QStringLiteral("filter admitted %1 third-party cookie(s)").arg(admitted3p));
    tpAdmitted ? ++pass : ++fail;

    // --- Restore the shipped defaults ---------------------------------------
    m_config->setBlockThirdPartyCookies(true);
    m_config->setSendDoNotTrack(true);
    m_config->setSendGlobalPrivacyControl(true);
    m_config->setSimplifiedUserAgent(true);
    QApplication::processEvents(QEventLoop::AllEvents, 100);

    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

int BrowserApplication::runSmokeTlsBad(const QUrl &tlsUrl, const QUrl &goodUrl)
{
    int pass = 0, fail = 0;

    // Automated runs reject every certificate error with the safe default
    // (no modal dialog under Xvfb); this only ever makes the browser more
    // restrictive, never less.
    QCoreApplication::instance()->setProperty("norAutoRejectCertificates", true);

    // Negative control first: the self-signed endpoint must FAIL to load.
    BrowserWindow *window = createWindow(false, tlsUrl);
    WebView *view = window->currentView();
    const bool loaded = waitForLoad(view, 20000);
    const QString title = view->page()->title();
    const bool rejected = !loaded && !title.contains(QLatin1String("Hello"));
    smokeLine("tls/self-signed-rejected", rejected,
              QStringLiteral("loadOk=%1 title='%2'")
                  .arg(loaded)
                  .arg(title));
    rejected ? ++pass : ++fail;

    // Positive control on the same run: a valid HTTPS page still loads,
    // proving the rejection was certificate-specific.
    if (goodUrl.isValid()) {
        view->load(goodUrl);
        const bool goodLoaded = waitForLoad(view, 30000);
        smokeLine("tls/valid-https-still-loads", goodLoaded,
                  QStringLiteral("url=%1").arg(view->page()->url().toString()));
        goodLoaded ? ++pass : ++fail;
    }

    QCoreApplication::instance()->setProperty("norAutoRejectCertificates", false);
    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

int BrowserApplication::runSmokeLanguage(const QString &screenshotDir)
{
    int pass = 0, fail = 0;
    BrowserWindow *window = createWindow(false);
    // Give the new-tab document time to load AND paint so the screenshots
    // below capture real content (grab() right after processEvents() catches
    // an unpainted view).
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(900);
    QApplication::processEvents(QEventLoop::AllEvents, 200);

    // The new-tab page must actually render its widgets: a search field and
    // visible text must exist in the DOM.
    QString newtabReady;
    window->currentView()->page()->runJavaScript(
        QStringLiteral("(function(){var s=document.querySelector('input');"
                       "var body=(document.body&&document.body.innerText||'')"
                       ".replace(/[\\s]+/g,' ').trim();"
                       "return JSON.stringify({hasSearch:!!s,"
                       "bodyLen:body.length,bodyStart:body.slice(0,40)});})()"),
        [&newtabReady](const QVariant &v) { newtabReady = v.toString(); });
    {
        QElapsedTimer wait;
        wait.start();
        while (newtabReady.isEmpty() && wait.elapsed() < 5000)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    const QJsonObject newtab =
        QJsonDocument::fromJson(newtabReady.toUtf8()).object();
    const bool newtabOk = newtab.value(QLatin1String("hasSearch")).toBool()
                          && newtab.value(QLatin1String("bodyLen")).toInt() > 0;
    smokeLine("lang/newtab-renders", newtabOk,
              QStringLiteral("hasSearch=%1 body='%2'")
                  .arg(newtab.value(QLatin1String("hasSearch")).toBool())
                  .arg(newtab.value(QLatin1String("bodyStart")).toString()));
    newtabOk ? ++pass : ++fail;

    QApplication::processEvents();

    // English (default).
    const bool enDefault = m_languages->currentLanguage() == QStringLiteral("en");
    smokeLine("lang/default-en", enDefault);
    enDefault ? ++pass : ++fail;

    // Arabic: translator + RTL.
    m_languages->setLanguage(QStringLiteral("ar"));
    QApplication::processEvents();
    const QString translatedTab = QApplication::translate("TabWidget", "New Tab");
    const bool arLoaded = translatedTab != QStringLiteral("New Tab");
    const bool arRtl = QApplication::layoutDirection() == Qt::RightToLeft;
    smokeLine("lang/ar-loaded+rtl", arLoaded && arRtl, translatedTab);
    arLoaded && arRtl ? ++pass : ++fail;

    if (!screenshotDir.isEmpty()) {
        QDir().mkpath(screenshotDir);
        window->grab().save(screenshotDir + QStringLiteral("/newtab-ar.png"));
    }

    // French: translator + LTR.
    m_languages->setLanguage(QStringLiteral("fr"));
    QApplication::processEvents();
    const QString frTab = QApplication::translate("TabWidget", "New Tab");
    const bool frLoaded = frTab != QStringLiteral("New Tab");
    const bool frLtr = QApplication::layoutDirection() == Qt::LeftToRight;
    smokeLine("lang/fr-loaded+ltr", frLoaded && frLtr, frTab);
    frLoaded && frLtr ? ++pass : ++fail;

    if (!screenshotDir.isEmpty())
        window->grab().save(screenshotDir + QStringLiteral("/newtab-fr.png"));

    // Persistence.
    m_languages->setLanguage(QStringLiteral("ar"));
    const bool persisted = m_config->language() == QStringLiteral("ar");
    smokeLine("lang/persisted", persisted);
    persisted ? ++pass : ++fail;

    // Restore English.
    m_languages->setLanguage(QStringLiteral("en"));

    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------- topsites --

// Runs a JS snippet on the view's page and waits for the callback.
static QString probeJson(WebView *view, const QString &js, int timeoutMs = 5000)
{
    QString out;
    view->page()->runJavaScript(js, [&out](const QVariant &v) { out = v.toString(); });
    QElapsedTimer wait;
    wait.start();
    while (out.isEmpty() && wait.elapsed() < timeoutMs)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    return out;
}

int BrowserApplication::runSmokeTopSites(const QString &baseUrl, const QString &screenshotDir)
{
    int pass = 0, fail = 0;
    const QString shots = screenshotDir.isEmpty() ? QString() : screenshotDir;
    if (!shots.isEmpty())
        QDir().mkpath(shots);

    auto jsonField = [](const QString &json, const char *name) -> QJsonValue {
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        return doc.object().value(QLatin1String(name));
    };
    auto shot = [this, &shots](const QString &name) {
        if (shots.isEmpty() || m_windows.isEmpty())
            return;
        BrowserWindow *w = m_windows.first();
        if (w)
            w->grab().save(shots + QLatin1Char('/') + name);
    };
    auto goToNewTab = [](BrowserWindow *w) {
        if (WebView *v = w->currentView())
            v->load(QUrl(QStringLiteral("nor://newtab")));
    };
    auto pageAction = [](BrowserWindow *w, const QString &js) {
        if (WebView *v = w->currentView())
            v->page()->runJavaScript(js, [](const QVariant &) {});
    };

    // ------------------------------------------------------------------
    // 1) NEW USER: empty history -> no tiles, graceful hint, no broken
    //    layout, and NO fixed/curated shortcuts anywhere in the DOM.
    // ------------------------------------------------------------------
    BrowserWindow *window = createWindow(false);
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(700);
    QString j = probeJson(window->currentView(),
        QStringLiteral("(function(){return JSON.stringify({"
                       "tiles:document.querySelectorAll('a.site').length,"
                       "hint:!!document.querySelector('.empty-hint'),"
                       "body:(document.body?document.body.innerText:''),"
                       "scroll:document.documentElement?document.documentElement.scrollHeight:0,"
                       "vh:window.innerHeight"
                       "});})()"));
    const int emptyTiles = jsonField(j, "tiles").toInt(-1);
    const bool emptyHint = jsonField(j, "hint").toBool();
    const QString emptyBody = jsonField(j, "body").toString();
    bool noCurated = true;
    for (const char *banned : { "DuckDuckGo", "Wikipedia", "Hacker News",
                                "Stack Overflow", "MDN" }) {
        if (emptyBody.contains(QLatin1String(banned)))
            noCurated = false;
    }
    const bool emptyOk = emptyTiles == 0 && emptyHint && noCurated;
    smokeLine("topsites/new-user-empty", emptyOk,
              QStringLiteral("tiles=%1 hint=%2 curated=%3")
                  .arg(emptyTiles).arg(emptyHint).arg(!noCurated));
    emptyOk ? ++pass : ++fail;
    shot(QStringLiteral("newtab-empty.png"));

    // ------------------------------------------------------------------
    // 2) SEEDED HISTORY: frecency ranking (count x recency decay), host
    //    grouping, stale sites pushed out, one tile per host.
    // ------------------------------------------------------------------
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    auto seed = [this](const char *url, const char *title, int visits, qint64 when) {
        for (int i = 0; i < visits; ++i)
            m_history->addVisit(QString::fromLatin1(url), QString::fromLatin1(title), when);
    };
    seed("https://example.org/", "Example Domain", 30, now - 2 * 86400);   // score ~27
    seed("https://github.com/", "GitHub", 12, now - 4 * 86400);            // score ~10
    seed("https://news.ycombinator.com/", "Hacker News", 6, now - 86400);  // score ~5.7
    seed("https://developer.mozilla.org/", "MDN", 5, now - 10 * 86400);    // score ~2.5
    seed("https://www.wikipedia.org/", "Wikipedia", 3, now - 30 * 86400);  // score ~0.66
    seed("https://stale.example/", "Stale Site", 40, now - 400 * 86400);   // ~0.0001
    seed("https://github.com/nor-browser", "Deep GitHub Link", 2, now - 86400); // merges into github.com host
    QTest::qWait(400); // debounce refresh

    goToNewTab(window);
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(600);
    j = probeJson(window->currentView(),
        QStringLiteral("(function(){var t=[].map.call(document.querySelectorAll('a.site'),"
                       "function(a){return {h:new URL(a.href).host,p:a.dataset.pinned};});"
                       "return JSON.stringify({tiles:t.length,hosts:t,"
                       "stale:!![].some.call(document.querySelectorAll('a.site'),"
                       "function(a){return a.href.indexOf('stale.example')>=0;})});})()"));
    const int seededTiles = jsonField(j, "tiles").toInt(-1);
    QStringList hosts;
    for (const QJsonValue &v : jsonField(j, "hosts").toArray())
        hosts.append(v.toObject().value(QLatin1String("h")).toString());
    // 40 visits but 400 days old: decays to the BOTTOM of the list.
    const bool staleLast = !hosts.isEmpty()
                           && hosts.last() == QLatin1String("stale.example");
    const bool orderOk = hosts.size() >= 4
                         && hosts.at(0) == QLatin1String("example.org")
                         && hosts.at(1) == QLatin1String("github.com");
    const bool dedupOk = hosts.count(QLatin1String("github.com")) == 1;
    const bool rankedOk = seededTiles == 6 && orderOk && dedupOk && staleLast;
    smokeLine("topsites/ranking+grouping", rankedOk,
              QStringLiteral("tiles=%1 hosts=%2").arg(seededTiles).arg(hosts.join(QLatin1Char(','))));
    rankedOk ? ++pass : ++fail;
    shot(QStringLiteral("newtab-seeded.png"));

    // ------------------------------------------------------------------
    // 2b) FAVICON: a locally captured icon is served through
    //     nor://favicon/<host> and replaces the letter tile.
    // ------------------------------------------------------------------
    if (m_favicons) {
        QPixmap pm(64, 64);
        pm.fill(QColor(0x8b, 0x5c, 0xf6));
        m_favicons->capture(QIcon(pm), QUrl(QStringLiteral("https://example.org/")));
        QTest::qWait(200);
        goToNewTab(window);
        waitForLoad(window->currentView(), 20000);
        QTest::qWait(600);
        j = probeJson(window->currentView(),
            QStringLiteral("(function(){var a=[].filter.call("
                           "document.querySelectorAll('a.site'),"
                           "function(a){return a.href.indexOf('example.org')>=0;})[0];"
                           "if(!a)return JSON.stringify({found:false});"
                           "var img=a.querySelector('img');"
                           "return JSON.stringify({found:true,hasImg:!!img,"
                           "loaded:!!(img&&img.naturalWidth>0),"
                           "src:img?img.src:''});})()"));
        const bool favFound = jsonField(j, "found").toBool();
        const bool favLoaded = jsonField(j, "hasImg").toBool()
                               && jsonField(j, "loaded").toBool();
        const QString favSrc = jsonField(j, "src").toString();
        const bool faviconOk = favFound && favLoaded
                               && favSrc == QStringLiteral("nor://favicon/example.org");
        smokeLine("topsites/favicon-served", faviconOk,
                  QStringLiteral("src=%1 loaded=%2").arg(favSrc).arg(favLoaded));
        faviconOk ? ++pass : ++fail;
        shot(QStringLiteral("newtab-favicon.png"));
    }

    // ------------------------------------------------------------------
    // 3) PIN through the real page action channel.
    // ------------------------------------------------------------------
    const QString pinUrl = QStringLiteral("https://github.com/");
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/pin?url='"
        "+encodeURIComponent('%1')+'&title='+encodeURIComponent('GitHub');").arg(pinUrl));
    QTest::qWait(300);
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(600);
    j = probeJson(window->currentView(),
        QStringLiteral("(function(){var a=document.querySelector('a.site');"
                       "return JSON.stringify({url:a?a.href:'',pinned:a?a.dataset.pinned:'0',"
                       "pinmark:document.querySelectorAll('.pin-mark').length});})()"));
    const bool pinDom = jsonField(j, "url").toString() == pinUrl
                        && jsonField(j, "pinned").toString() == QLatin1String("1")
                        && jsonField(j, "pinmark").toInt() == 1;
    const bool pinStore = m_topSites && m_topSites->isPinned(QUrl(pinUrl));
    smokeLine("topsites/pin", pinDom && pinStore,
              QStringLiteral("dom=%1 store=%2").arg(pinDom).arg(pinStore));
    (pinDom && pinStore) ? ++pass : ++fail;
    shot(QStringLiteral("newtab-pinned.png"));

    // ------------------------------------------------------------------
    // 4) EDIT: rename + change URL (becomes a pin with the new values).
    // ------------------------------------------------------------------
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/edit?url='"
        "+encodeURIComponent('%1')+'&title='+encodeURIComponent('NOR Repo')"
        "+'&newurl='+encodeURIComponent('https://github.com/nor-browser');").arg(pinUrl));
    QTest::qWait(300);
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(600);
    j = probeJson(window->currentView(),
        QStringLiteral("(function(){var t=[].map.call(document.querySelectorAll('a.site'),"
                       "function(a){return a.href+'|'+a.dataset.title+'|'+a.dataset.pinned;});"
                       "return JSON.stringify({tiles:t});})()"));
    QStringList tilesAfterEdit;
    for (const QJsonValue &v : jsonField(j, "tiles").toArray())
        tilesAfterEdit.append(v.toString());
    const bool editOk = tilesAfterEdit.contains(
        QStringLiteral("https://github.com/nor-browser|NOR Repo|1"))
        && !tilesAfterEdit.contains(QStringLiteral("https://github.com/|GitHub|1"));
    smokeLine("topsites/edit", editOk, tilesAfterEdit.join(QLatin1Char(' ')));
    editOk ? ++pass : ++fail;

    // ------------------------------------------------------------------
    // 5) REMOVE a derived entry (host hidden).
    // ------------------------------------------------------------------
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/remove?url='"
        "+encodeURIComponent('https://example.org/');"));
    QTest::qWait(300);
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(600);
    j = probeJson(window->currentView(),
        QStringLiteral("(function(){return JSON.stringify({"
                       "tiles:document.querySelectorAll('a.site').length,"
                       "gone:![].some.call(document.querySelectorAll('a.site'),"
                       "function(a){return a.href.indexOf('example.org')>=0;})});})()"));
    const bool removedOk = jsonField(j, "gone").toBool()
                           && jsonField(j, "tiles").toInt() >= 1;
    smokeLine("topsites/remove", removedOk);
    removedOk ? ++pass : ++fail;

    // ------------------------------------------------------------------
    // 6) PRIVATE WINDOW: read-only tiles, zero pollution of the real list.
    // ------------------------------------------------------------------
    const int historyBefore = int(m_history->count());
    BrowserWindow *privateWindow = createWindow(true);
    waitForLoad(privateWindow->currentView(), 20000);
    QTest::qWait(600);
    j = probeJson(privateWindow->currentView(),
        QStringLiteral("(function(){return JSON.stringify({"
                       "tiles:document.querySelectorAll('a.site').length,"
                       "readonly:(typeof READONLY!=='undefined'&&READONLY)});})()"));
    const int privTiles = jsonField(j, "tiles").toInt(-1);
    const bool privReadonly = jsonField(j, "readonly").toBool();
    // Pin attempt from the private page must be refused in C++.
    pageAction(privateWindow, QStringLiteral(
        "location.href='nor://newtab/action/pin?url='"
        "+encodeURIComponent('https://private-pollution.example/')+'&title=x';"));
    QTest::qWait(400);
    // A real page visit in the private window must not touch history.
    privateWindow->openInNewForegroundTab(QUrl(baseUrl + QStringLiteral("/hello")));
    waitForLoad(privateWindow->currentView(), 20000);
    QTest::qWait(400);
    const int historyAfter = int(m_history->count());
    const bool noPollution = historyAfter == historyBefore
                             && !(m_topSites
                                  && m_topSites->isPinned(
                                      QUrl(QStringLiteral("https://private-pollution.example/"))));
    const bool privOk = privTiles >= 1 && privReadonly && noPollution;
    smokeLine("topsites/private-isolation", privOk,
              QStringLiteral("tiles=%1 readonly=%2 history %3->%4")
                  .arg(privTiles).arg(privReadonly).arg(historyBefore).arg(historyAfter));
    privOk ? ++pass : ++fail;
    shot(QStringLiteral("newtab-private.png"));
    privateWindow->close();
    QTest::qWait(400);

    // ------------------------------------------------------------------
    // 7) CLEAR HISTORY: derived tiles vanish with it, pins survive,
    //    dismissals are forgotten (history-derived data).
    // ------------------------------------------------------------------
    m_history->clearAll();
    QTest::qWait(400);
    seed("https://example.org/", "Example Domain", 3, now - 86400);
    QTest::qWait(400);
    goToNewTab(window);
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(600);
    j = probeJson(window->currentView(),
        QStringLiteral("(function(){var t=[].map.call(document.querySelectorAll('a.site'),"
                       "function(a){return a.href+'|'+a.dataset.pinned;});"
                       "return JSON.stringify({tiles:t});})()"));
    QStringList tilesAfterClear;
    for (const QJsonValue &v : jsonField(j, "tiles").toArray())
        tilesAfterClear.append(v.toString());
    const bool pinSurvived = tilesAfterClear.contains(
        QStringLiteral("https://github.com/nor-browser|1"));
    // example.org returns (its dismissal was history-derived data).
    int exampleTiles = 0;
    for (const QString &t : tilesAfterClear) {
        if (t.startsWith(QLatin1String("https://example.org/")))
            ++exampleTiles;
    }
    const bool dismissalReset = exampleTiles == 1;
    const bool clearOk = pinSurvived && dismissalReset
                         && tilesAfterClear.size() == 2;
    smokeLine("topsites/clear-history", clearOk, tilesAfterClear.join(QLatin1Char(' ')));
    clearOk ? ++pass : ++fail;
    shot(QStringLiteral("newtab-after-clear.png"));

    // ------------------------------------------------------------------
    // 8) VIEWPORT: no status bar; a Shorts-like bottom panel owns the
    //    window's bottom pixels (pixel-checked, not just DOM-checked).
    // ------------------------------------------------------------------
    window->openInNewForegroundTab(QUrl(baseUrl + QStringLiteral("/shorts")));
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(900);
    const bool noStatusBar = window->findChild<QStatusBar *>() == nullptr;
    const QImage grabbed = window->grab().toImage();
    // The page paints a #ff00aa marker stripe across its very bottom.
    // Sample several points along the bottom two rows.
    int markerHits = 0;
    for (int x = grabbed.width() / 6; x < grabbed.width() * 5 / 6; x += 24) {
        for (int y = grabbed.height() - 2; y <= grabbed.height() - 1; ++y) {
            const QRgb rgb = grabbed.pixel(x, y);
            if (qRed(rgb) > 200 && qGreen(rgb) < 90 && qBlue(rgb) > 90)
                ++markerHits;
        }
    }
    const bool viewportOk = noStatusBar && markerHits > 20;
    smokeLine("topsites/viewport-no-statusbar", viewportOk,
              QStringLiteral("statusBar=%1 markerHits=%2").arg(!noStatusBar).arg(markerHits));
    viewportOk ? ++pass : ++fail;
    shot(QStringLiteral("shorts-viewport.png"));

    // ------------------------------------------------------------------
    // 9) TAB SWITCH: every page has the chrome-matching background color
    //    (the white-flash fix) and no white band appears in captures
    //    taken immediately after switching.
    // ------------------------------------------------------------------
    window->openInNewForegroundTab(QUrl(baseUrl + QStringLiteral("/hello")));
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(500);
    bool bgOk = true;
    for (int i = 0; i < window->tabCount(); ++i) {
        if (WebView *v = window->tabWidget()->viewAt(i)) {
            if (v->page()->backgroundColor() != Theme::webBackground())
                bgOk = false;
        }
    }
    int whiteRows = 0;
    for (int s = 0; s < 4; ++s) {
        window->tabWidget()->setCurrentIndex(s % 2 == 0 ? 0 : 1);
        QApplication::processEvents(QEventLoop::AllEvents, 30);
        const QImage sw = window->grab().toImage();
        for (int y = sw.height() / 3; y < 2 * sw.height() / 3; y += 4) {
            int whiteRun = 0;
            for (int x = 0; x < sw.width(); ++x) {
                const QRgb rgb = sw.pixel(x, y);
                if (qRed(rgb) > 244 && qGreen(rgb) > 244 && qBlue(rgb) > 244)
                    ++whiteRun;
            }
            if (whiteRun == sw.width())
                ++whiteRows;
        }
    }
    const bool switchOk = bgOk && whiteRows == 0;
    smokeLine("topsites/tab-switch-flash", switchOk,
              QStringLiteral("bgOk=%1 whiteRows=%2").arg(bgOk).arg(whiteRows));
    switchOk ? ++pass : ++fail;
    shot(QStringLiteral("tab-switch.png"));

    // ------------------------------------------------------------------
    // 9b) RESIZE: the grid re-flows (no lone/stretched tiles, no
    //     horizontal overflow) at smaller window sizes.
    // ------------------------------------------------------------------
    int resizeOkCount = 0;
    const QSize originalSize = window->size();
    for (const QSize smaller : { QSize(900, 600), QSize(700, 480) }) {
        window->resize(smaller);
        QTest::qWait(500);
        goToNewTab(window);
        waitForLoad(window->currentView(), 20000);
        QTest::qWait(400);
        j = probeJson(window->currentView(),
            QStringLiteral("(function(){var g=document.querySelector('.grid');"
                           "var cols=g?(getComputedStyle(g).gridTemplateColumns"
                           ".split(' ').length):0;"
                           "return JSON.stringify({cols:cols,"
                           "overflowX:document.documentElement.scrollWidth"
                           ">window.innerWidth+1});})()"));
        const int cols = jsonField(j, "cols").toInt(-1);
        const bool noOverflow = !jsonField(j, "overflowX").toBool();
        // At 900px (~860 content) 6 tiles still fit in one row; at 700px
        // they must re-flow into more columns, never overflow.
        const bool okThis = noOverflow && cols >= 2 && cols <= 6;
        if (okThis)
            ++resizeOkCount;
    }
    window->resize(originalSize);
    QTest::qWait(300);
    const bool resizeOk = resizeOkCount == 2;
    smokeLine("topsites/window-resize", resizeOk,
              QStringLiteral("phases=%1/2").arg(resizeOkCount));
    resizeOk ? ++pass : ++fail;

    // ------------------------------------------------------------------
    // 10) RTL (Arabic): the grid follows the document direction.
    // ------------------------------------------------------------------
    m_languages->setLanguage(QStringLiteral("ar"));
    QTest::qWait(500);
    goToNewTab(window);
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(600);
    j = probeJson(window->currentView(),
        QStringLiteral("(function(){return JSON.stringify({"
                       "dir:document.documentElement.getAttribute('dir'),"
                       "tiles:document.querySelectorAll('a.site').length});})()"));
    const bool rtlOk = jsonField(j, "dir").toString() == QLatin1String("rtl")
                       && jsonField(j, "tiles").toInt() >= 1;
    smokeLine("topsites/rtl-arabic", rtlOk, jsonField(j, "dir").toString());
    rtlOk ? ++pass : ++fail;
    shot(QStringLiteral("newtab-ar.png"));
    m_languages->setLanguage(QStringLiteral("en"));

    window->close();
    QTest::qWait(300);

    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Chrome appearance matrix — comparable BEFORE/AFTER captures for redesigns.
// ---------------------------------------------------------------------------
int BrowserApplication::runSmokeChrome(const QString &screenshotDir)
{
    QDir().mkpath(screenshotDir);
    const QString shots = screenshotDir.endsWith(QLatin1Char('/'))
                              ? screenshotDir : screenshotDir + QLatin1Char('/');
    int pass = 0, fail = 0;

    auto shot = [this, &shots](const QString &name) {
        if (!m_windows.isEmpty() && m_windows.first())
            m_windows.first()->grab().save(shots + name);
    };
    auto loadUrl = [this](const QUrl &url) {
        if (m_windows.isEmpty() || !m_windows.first())
            return;
        if (WebView *v = m_windows.first()->currentView())
            v->load(url);
    };
    auto settle = [](int ms) { QTest::qWait(ms); };

    BrowserWindow *window = createWindow(false);
    window->resize(1366, 768);
    settle(500);

    // 1) New tab (new-user state).
    waitForLoad(window->currentView(), 20000);
    settle(700);
    shot(QStringLiteral("chrome-newtab.png"));

    // 2) Regular web page (no test server here: internal page is fine for
    //    chrome evaluation; load an about-blank-ish doc via newtab page).
    loadUrl(QUrl(QStringLiteral("nor://newtab")));
    waitForLoad(window->currentView(), 20000);
    settle(600);

    // 3) Many tabs: 7 more (8 total incl. current).
    for (int i = 0; i < 7; ++i) {
        window->openInNewBackgroundTab(QUrl(QStringLiteral("nor://newtab")));
        settle(120);
    }
    window->tabWidget()->setCurrentIndex(3);
    settle(500);
    shot(QStringLiteral("chrome-tabs-many.png"));
    smokeLine("chrome/many-tabs", window->tabWidget()->count() == 8,
              QStringLiteral("tabs=%1").arg(window->tabWidget()->count()));
    window->tabWidget()->count() == 8 ? ++pass : ++fail;

    // 4) Tab context menu: real right-click on a tab; a scheduled grab
    //    fires inside the menu's own event loop, then closes the popup.
    {
        QScreen *screen = window->screen();
        QTimer::singleShot(500, this, [screen, &shots]() {
            if (screen)
                screen->grabWindow(0).save(shots + QStringLiteral("chrome-tab-ctx.png"));
            if (QWidget *popup = QApplication::activePopupWidget())
                popup->close();
        });
        QTabBar *bar = window->tabWidget()->tabBar();
        QTest::mouseClick(bar, Qt::RightButton, Qt::NoModifier, bar->tabRect(2).center());
        settle(300);
    }

    // 5) Settings dialog (non-modal for capture).
    {
        SettingsDialog dialog(this, window);
        dialog.show();
        settle(500);
        dialog.grab().save(shots + QStringLiteral("chrome-settings.png"));
        dialog.hide();
        settle(200);
    }
    // 6) Downloads dialog.
    {
        DownloadsDialog dialog(this, window);
        dialog.show();
        settle(400);
        dialog.grab().save(shots + QStringLiteral("chrome-downloads.png"));
        dialog.hide();
        settle(200);
    }
    // 7) Bookmarks dialog (one bookmark seeded for realistic content).
    m_bookmarks->addBookmark(QStringLiteral("NØR Smoke Hello"),
                             QStringLiteral("http://127.0.0.1:8377/hello"));
    {
        BookmarksDialog dialog(this, window);
        dialog.show();
        settle(400);
        dialog.grab().save(shots + QStringLiteral("chrome-bookmarks.png"));
        dialog.hide();
        settle(200);
    }

    // 8) Fullscreen.
    window->onFullScreenToggled(true);
    settle(600);
    shot(QStringLiteral("chrome-fullscreen.png"));
    window->onFullScreenToggled(false);
    settle(400);

    // 9) Arabic RTL then French.
    m_languages->setLanguage(QStringLiteral("ar"));
    settle(400);
    loadUrl(QUrl(QStringLiteral("nor://newtab")));
    waitForLoad(window->currentView(), 20000);
    settle(600);
    shot(QStringLiteral("chrome-ar.png"));
    m_languages->setLanguage(QStringLiteral("fr"));
    settle(400);
    loadUrl(QUrl(QStringLiteral("nor://newtab")));
    waitForLoad(window->currentView(), 20000);
    settle(600);
    shot(QStringLiteral("chrome-fr.png"));
    m_languages->setLanguage(QStringLiteral("en"));
    settle(300);

    // 10) Resized (small) window.
    window->resize(940, 640);
    settle(500);
    shot(QStringLiteral("chrome-resized.png"));
    window->resize(1366, 768);
    settle(300);

    // 11) Private window.
    {
        BrowserWindow *priv = createWindow(true);
        priv->resize(1366, 768);
        settle(600);
        if (priv->grab().save(shots + QStringLiteral("chrome-private.png")))
            ++pass;
        else
            ++fail;
        priv->close();
        settle(300);
    }

    window->close();
    settle(300);
    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// New Tab experience matrix — Brave-inspired backgrounds + glassmorphism.
// ---------------------------------------------------------------------------
int BrowserApplication::runSmokeNewTab(const QString &screenshotDir)
{
    QDir().mkpath(screenshotDir);
    const QString shots = screenshotDir.endsWith(QLatin1Char('/'))
                              ? screenshotDir : screenshotDir + QLatin1Char('/');
    int pass = 0, fail = 0;

    auto shot = [this, &shots](const QString &name) {
        if (!m_windows.isEmpty() && m_windows.first())
            m_windows.first()->grab().save(shots + name);
    };
    auto jsonField = [](const QString &json, const char *name) -> QJsonValue {
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        return doc.object().value(QLatin1String(name));
    };
    auto pageAction = [](BrowserWindow *w, const QString &js) {
        if (WebView *v = w->currentView())
            v->page()->runJavaScript(js, [](const QVariant &) {});
    };
    auto reloadNewTab = [this](BrowserWindow *w) {
        if (WebView *v = w->currentView())
            v->load(QUrl(QStringLiteral("nor://newtab")));
        if (w && w->currentView())
            waitForLoad(w->currentView(), 20000);
    };
    auto bodyState = [this](BrowserWindow *w) {
        return probeJson(w->currentView(),
            QStringLiteral("(function(){var b=document.body,i=document.getElementById('bg');"
                           "return JSON.stringify({"
                           "bgOn:b.classList.contains('bg-on'),"
                           "dark:b.classList.contains('dark-bg'),"
                           "light:b.classList.contains('light-bg'),"
                           "noBlur:b.classList.contains('no-blur'),"
                           "src:i?i.getAttribute('src'):'',"
                           "loaded:!!(i&&i.naturalWidth>0),"
                           "tiles:document.querySelectorAll('a.site').length,"
                           "ts:document.getElementById('topsites').style.display!=='none'"
                           "});})()"));
    };

    BrowserWindow *window = createWindow(false);
    window->resize(1366, 768);
    QTest::qWait(500);
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(700);

    // ------------------------------------------------------------------
    // 1) DEFAULT background (dark): image actually loads through the
    //    nor://background channel, scheme flags applied, no network.
    // ------------------------------------------------------------------
    QString j = bodyState(window);
    bool ok = jsonField(j, "bgOn").toBool() && jsonField(j, "dark").toBool()
              && jsonField(j, "loaded").toBool()
              && jsonField(j, "src").toString() == QStringLiteral("nor://background/nor-graphite");
    smokeLine("newtab/bg-default-dark", ok, j);
    ok ? ++pass : ++fail;
    shot(QStringLiteral("newtab-bg-default.png"));

    // ------------------------------------------------------------------
    // 2) LIGHT background (Dawn): text scheme flips with the artwork.
    // ------------------------------------------------------------------
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/bg-set?id=nor-zenith';"));
    QTest::qWait(500);
    j = bodyState(window);
    ok = jsonField(j, "bgOn").toBool() && jsonField(j, "light").toBool()
         && jsonField(j, "src").toString() == QStringLiteral("nor://background/nor-zenith");
    smokeLine("newtab/bg-light", ok, j);
    ok ? ++pass : ++fail;
    shot(QStringLiteral("newtab-bg-light.png"));

    // ------------------------------------------------------------------
    // 3) BUSY-DETAIL background (Deep Space): still readable (veil+glass).
    // ------------------------------------------------------------------
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/bg-set?id=nor-nebula';"));
    QTest::qWait(500);
    j = bodyState(window);
    ok = jsonField(j, "bgOn").toBool() && jsonField(j, "loaded").toBool();
    smokeLine("newtab/bg-busy-detail", ok, j);
    ok ? ++pass : ++fail;
    shot(QStringLiteral("newtab-bg-busy.png"));

    // ------------------------------------------------------------------
    // 4) CUSTOM imported image: import pipeline (downscale+save+persist).
    // ------------------------------------------------------------------
    {
        QImage img(1400, 900, QImage::Format_RGB32);
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const double t = double(y) / img.height();
                img.setPixel(x, y, QColor(int(26 + 30 * t), int(22 + 18 * t),
                                          int(46 + 40 * t)).rgb());
            }
        }
        const QString tmp = QDir::temp().filePath(QStringLiteral("nor-smoke-bg.png"));
        img.save(tmp, "PNG");
        pageAction(window, QStringLiteral(
            "location.href='nor://newtab/action/bg-import?path='"
            "+encodeURIComponent('%1');").arg(tmp));
        QTest::qWait(900);                       // import + reload
        waitForLoad(window->currentView(), 20000);
        QTest::qWait(700);
        j = bodyState(window);
        const QString src = jsonField(j, "src").toString();
        ok = jsonField(j, "bgOn").toBool() && jsonField(j, "loaded").toBool()
             && src.startsWith(QLatin1String("nor://background/custom-"));
        smokeLine("newtab/bg-imported", ok,
                  QStringLiteral("src=%1").arg(src));
        ok ? ++pass : ++fail;
        shot(QStringLiteral("newtab-bg-custom.png"));

        // Persistence: a brand-new window (fresh new-tab page) still uses
        // the imported background — the choice survived on disk.
        reloadNewTab(window);
        j = bodyState(window);
        const bool persisted = jsonField(j, "src").toString()
                                   .startsWith(QLatin1String("nor://background/custom-"));
        const QString savedId = m_config->newTabBackgroundId();
        smokeLine("newtab/bg-persisted", persisted && savedId.startsWith(QLatin1String("custom-")),
                  QStringLiteral("id=%1").arg(savedId));
        (persisted && savedId.startsWith(QLatin1String("custom-"))) ? ++pass : ++fail;
    }

    // ------------------------------------------------------------------
    // 5) BLUR off: fallback class + still rendered; then back on.
    // ------------------------------------------------------------------
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/newtab-pref?blur=0';"));
    QTest::qWait(300);
    j = bodyState(window);
    ok = jsonField(j, "noBlur").toBool();
    smokeLine("newtab/blur-off-fallback", ok, j);
    ok ? ++pass : ++fail;
    shot(QStringLiteral("newtab-blur-off.png"));
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/newtab-pref?blur=1';"));
    QTest::qWait(300);

    // ------------------------------------------------------------------
    // 6) Search bar: focus + typing NEVER change its geometry (Brave-style
    //    stability), and the customization panel exposes the full gallery.
    // ------------------------------------------------------------------
    j = probeJson(window->currentView(), QStringLiteral(
        "(function(){var f=document.querySelector('form.search');"
        "var r1=f.getBoundingClientRect();"
        "var i=document.getElementById('q');i.focus();"
        "i.value='nor browser polish 2026 typed query';"
        "var r2=f.getBoundingClientRect();"
        "return JSON.stringify({w1:r1.width,h1:r1.height,w2:r2.width,h2:r2.height,"
        "focused:document.activeElement===i,len:i.value.length});})()"));
    ok = jsonField(j, "w1").toDouble() > 0
         && qFuzzyCompare(jsonField(j, "w1").toDouble(), jsonField(j, "w2").toDouble())
         && jsonField(j, "h1").toDouble() == jsonField(j, "h2").toDouble()
         && jsonField(j, "focused").toBool() && jsonField(j, "len").toInt() > 10;
    smokeLine("newtab/search-stable-focus", ok, j);
    ok ? ++pass : ++fail;
    QTest::qWait(400);
    shot(QStringLiteral("newtab-search-focus.png"));

    j = probeJson(window->currentView(), QStringLiteral(
        "(function(){document.getElementById('panel').classList.add('open');"
        "return '1';})()"));
    QTest::qWait(400);
    j = probeJson(window->currentView(), QStringLiteral(
        "(function(){return JSON.stringify({"
        "thumbs:document.querySelectorAll('#gallery .thumb').length,"
        "selected:document.querySelectorAll('#gallery .thumb.selected').length,"
        "slider:!!document.getElementById('opacity'),"
        "blurChk:document.getElementById('chkBlur').checked"
        "});})()"));
    // 8 bundled + 1 imported + 1 "solid color" entry.
    ok = jsonField(j, "thumbs").toInt() == 10
         && jsonField(j, "selected").toInt() == 1
         && jsonField(j, "slider").toBool() && jsonField(j, "blurChk").toBool();
    smokeLine("newtab/customize-panel", ok, j);
    ok ? ++pass : ++fail;
    shot(QStringLiteral("newtab-customize.png"));
    pageAction(window, QStringLiteral(
        "document.getElementById('panel').classList.remove('open');"));

    // ------------------------------------------------------------------
    // 7) Solid color mode: bg-set with an empty id disables the image.
    // ------------------------------------------------------------------
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/bg-set?id=';"));
    QTest::qWait(400);
    j = bodyState(window);
    ok = !jsonField(j, "bgOn").toBool();
    smokeLine("newtab/solid-color-mode", ok, j);
    ok ? ++pass : ++fail;
    shot(QStringLiteral("newtab-solid.png"));

    // ------------------------------------------------------------------
    // 8) RESET: back to the shipped defaults (nor-graphite, blur on).
    // ------------------------------------------------------------------
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/newtab-reset';"));
    QTest::qWait(600);
    waitForLoad(window->currentView(), 20000);
    QTest::qWait(700);
    j = bodyState(window);
    ok = jsonField(j, "bgOn").toBool()
         && jsonField(j, "src").toString() == QStringLiteral("nor://background/nor-graphite")
         && !jsonField(j, "noBlur").toBool()
         && jsonField(j, "ts").toBool();   // Top Sites visible again
    smokeLine("newtab/reset-defaults", ok, j);
    ok ? ++pass : ++fail;

    // ------------------------------------------------------------------
    // 9) Top-Sites visibility toggle (live + persisted).
    // ------------------------------------------------------------------
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/newtab-pref?topsites=0';"));
    QTest::qWait(300);
    j = bodyState(window);
    ok = !jsonField(j, "ts").toBool();
    smokeLine("newtab/topsites-hidden", ok, j);
    ok ? ++pass : ++fail;
    pageAction(window, QStringLiteral(
        "location.href='nor://newtab/action/newtab-pref?topsites=1';"));
    QTest::qWait(300);

    // ------------------------------------------------------------------
    // 10) RTL (Arabic) then French at 1366x768.
    // ------------------------------------------------------------------
    m_languages->setLanguage(QStringLiteral("ar"));
    QTest::qWait(500);
    reloadNewTab(window);
    QTest::qWait(600);
    j = probeJson(window->currentView(), QStringLiteral(
        "(function(){return JSON.stringify({"
        "dir:document.documentElement.getAttribute('dir'),"
        "bgOn:document.body.classList.contains('bg-on'),"
        "tiles:document.querySelectorAll('a.site').length});})()"));
    ok = jsonField(j, "dir").toString() == QLatin1String("rtl")
         && jsonField(j, "bgOn").toBool();
    smokeLine("newtab/rtl-arabic", ok, jsonField(j, "dir").toString());
    ok ? ++pass : ++fail;
    shot(QStringLiteral("newtab-ar.png"));
    m_languages->setLanguage(QStringLiteral("fr"));
    QTest::qWait(500);
    reloadNewTab(window);
    QTest::qWait(600);
    j = probeJson(window->currentView(), QStringLiteral(
        "(function(){return JSON.stringify({"
        "dir:document.documentElement.getAttribute('dir'),"
        "ph:document.getElementById('q').placeholder});})()"));
    ok = jsonField(j, "dir").toString() == QLatin1String("ltr")
         && jsonField(j, "ph").toString().contains(QLatin1String("Rechercher"));
    smokeLine("newtab/french", ok, jsonField(j, "ph").toString());
    ok ? ++pass : ++fail;
    shot(QStringLiteral("newtab-fr.png"));
    m_languages->setLanguage(QStringLiteral("en"));
    QTest::qWait(400);

    // ------------------------------------------------------------------
    // 11) TITLE SYNC: window title == address bar == active tab == the
    //     page actually in the viewport, through success, FAILURE and tab
    //     switches (the "7-Zip title over New Tab content" guard).
    // ------------------------------------------------------------------
    {
        WebView *view = window->currentView();
        view->load(QUrl(QStringLiteral("nor://newtab")));
        waitForLoad(view, 20000);
        QTest::qWait(500);
        const QString hello = QStringLiteral("http://127.0.0.1:8377/hello");
        view->load(QUrl(hello));
        waitForLoad(view, 20000);
        waitForTitle(view, 5000);
        QTest::qWait(400);
        const QString wt = window->windowTitle();
        const QString tt = window->tabWidget()->tabBar()->tabText(0);
        const QString at = window->addressBar() ? window->addressBar()->text() : QString();
        j = probeJson(view, QStringLiteral(
            "(function(){return JSON.stringify({t:document.title,"
            "logo:!!document.querySelector('.logo')});})()"));
        ok = wt.contains(QStringLiteral("NOR Smoke"))
             && tt == jsonField(j, "t").toString()
             && at == hello
             && !jsonField(j, "logo").toBool();
        smokeLine("newtab/titlesync-loaded", ok,
                  QStringLiteral("win='%1' tab='%2' addr='%3'")
                      .arg(wt, tt, at));
        ok ? ++pass : ++fail;
        shot(QStringLiteral("newtab-titlesync-loaded.png"));

        // Switch to a fresh New Tab: every surface agrees on "New Tab".
        window->openInNewForegroundTab(QUrl());
        QTest::qWait(800);
        waitForLoad(window->currentView(), 20000);
        QTest::qWait(500);
        const QString wtNt = window->windowTitle();
        const QString ttNt = window->tabWidget()->tabBar()->tabText(
            window->tabWidget()->currentIndex());
        const QString atNt = window->addressBar() ? window->addressBar()->text() : QString();
        j = probeJson(window->currentView(), QStringLiteral(
            "(function(){return JSON.stringify({logo:!!document.querySelector('.logo'),"
            "t:document.title});})()"));
        ok = wtNt.startsWith(QStringLiteral("New Tab"))
             && ttNt == QStringLiteral("New Tab")
             && atNt.isEmpty()
             && jsonField(j, "logo").toBool();
        smokeLine("newtab/titlesync-newtab", ok,
                  QStringLiteral("win='%1' tab='%2' addr='%3'")
                      .arg(wtNt, ttNt, atNt));
        ok ? ++pass : ++fail;
        shot(QStringLiteral("newtab-titlesync-newtab.png"));

        // Switch BACK to the hello tab: the full loaded-page state is
        // restored on every surface at once.
        window->tabWidget()->setCurrentIndex(0);
        QTest::qWait(800);
        const QString wtBack = window->windowTitle();
        const QString ttBack = window->tabWidget()->tabBar()->tabText(0);
        const QString atBack = window->addressBar() ? window->addressBar()->text() : QString();
        j = probeJson(window->currentView(), QStringLiteral(
            "(function(){return JSON.stringify({t:document.title});})()"));
        ok = wtBack.contains(QStringLiteral("NOR Smoke"))
             && ttBack == jsonField(j, "t").toString()
             && atBack == hello;
        smokeLine("newtab/titlesync-switchback", ok,
                  QStringLiteral("win='%1' tab='%2' addr='%3'")
                      .arg(wtBack, ttBack, atBack));
        ok ? ++pass : ++fail;

        // FAILED navigation LAST (the tab keeps the error page): no stale
        // title survives — the "7-Zip title over New Tab content" guard.
        view = window->currentView();
        const QString dead = QStringLiteral("http://nonexistent.invalid/x");
        view->load(QUrl(dead));
        waitForLoad(view, 8000);
        QTest::qWait(600);
        const QString wtFail = window->windowTitle();
        const QString ttFail = window->tabWidget()->tabBar()->tabText(0);
        const bool noStale = !wtFail.contains(QStringLiteral("NOR Smoke"))
                             && !ttFail.contains(QStringLiteral("NOR Smoke"));
        const QString atFail = window->addressBar() ? window->addressBar()->text() : QString();
        const bool addrSynced = atFail == window->currentView()->page()->url().toString()
                                || atFail.isEmpty();
        ok = noStale && addrSynced;
        smokeLine("newtab/titlesync-failed-load", ok,
                  QStringLiteral("win='%1' tab='%2' addr='%3'")
                      .arg(wtFail, ttFail, atFail));
        ok ? ++pass : ++fail;
        shot(QStringLiteral("newtab-titlesync-failed.png"));
    }

    window->close();
    QTest::qWait(300);
    printf("SMOKE RESULT pass=%d fail=%d\n", pass, fail);
    fflush(stdout);
    return fail == 0 ? 0 : 1;
}
