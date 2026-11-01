// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
// Unit tests for FilterListManager: remote list downloads, conditional
// requests (ETag/304), corrupt-download rejection, offline behaviour and
// the serialized engine startup cache.
//
// The network parts run against the local test server
// (tests/scripts/testserver.py), started on a free port by this test.
#include <QtTest>

#include "AdBlockClient.h"
#include "AdblockBackend.h"
#include "AppConfig.h"
#include "FilterListManager.h"
#include "Paths.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class TestFilterLists : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void plausibleFilterListValidation();
    void cacheFileNameIsStableAndSafe();
    void downloadGoodList();
    void conditionalRequestReturns304();
    void corruptDownloadRejected();
    void htmlErrorPageRejected();
    void offlineFailureKeepsCache();
    void staleUrlCacheRemoved();
    void engineCacheSpeedsUpStartup();
    void corruptEngineCacheFallsBack();

private:
    QString startServer();
    bool waitForSignal(QObject *sender, const char *signal, int timeoutMs);
    QPair<int, int> runUpdate(FilterListManager &manager);
    void removeCachedLists();

    QTemporaryDir m_dir;
    QProcess m_server;
    QString m_baseUrl;
    int m_port = 0;
};

void TestFilterLists::initTestCase()
{
    // Isolated application-data tree for every test.
    qputenv("XDG_DATA_HOME", m_dir.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", m_dir.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", m_dir.filePath("config").toUtf8());
    QDir().mkpath(m_dir.filePath("data"));
    QDir().mkpath(m_dir.filePath("cache"));
    QDir().mkpath(m_dir.filePath("config"));

    m_baseUrl = startServer();
    QVERIFY(!m_baseUrl.isEmpty());
}

QString TestFilterLists::startServer()
{
    // Pick a free port, then start the python test server on it.
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost);
    m_port = probe.serverPort();
    probe.close();

    const QString script = QDir(QLatin1String(NOR_FIXTURES_DIR))
                               .filePath(QStringLiteral("../scripts/testserver.py"));
    if (!QFile::exists(script))
        return QString();

    m_server.start(QStringLiteral("python3"),
                   QStringList() << script << QString::number(m_port));
    if (!m_server.waitForStarted(5000))
        return QString();

    // Wait until the server answers /hello.
    for (int i = 0; i < 50; ++i) {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, m_port);
        if (socket.waitForConnected(200)) {
            socket.disconnectFromHost();
            return QStringLiteral("http://127.0.0.1:%1").arg(m_port);
        }
        QTest::qWait(100);
    }
    return QString();
}

void TestFilterLists::cleanupTestCase()
{
    if (m_server.state() == QProcess::Running) {
        m_server.terminate();
        if (!m_server.waitForFinished(3000))
            m_server.kill();
    }
}

bool TestFilterLists::waitForSignal(QObject *sender, const char *signal, int timeoutMs)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(sender, signal, &loop, SLOT(quit()));
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    return timer.isActive();
}

// Runs updateNow() and waits (robustly) for updateFinished, returning its
// (changed, failed) arguments from the first emission.
QPair<int, int> TestFilterLists::runUpdate(FilterListManager &manager)
{
    QSignalSpy spy(&manager, &FilterListManager::updateFinished);
    manager.updateNow();
    if (spy.isEmpty() && !spy.wait(15000))
        return qMakePair(-1, -1);
    const QVariantList args = spy.takeFirst();
    return qMakePair(args.at(0).toInt(), args.at(1).toInt());
}

void TestFilterLists::removeCachedLists()
{
    QDir dir(Paths::filterListsDir());
    for (const QFileInfo &entry : dir.entryInfoList(QDir::Files))
        QFile::remove(entry.absoluteFilePath());
}

// ------------------------------------------------------------ validation ----

void TestFilterLists::plausibleFilterListValidation()
{
    // Above the size threshold with mostly rule-like lines.
    QString good = QStringLiteral(
        "!\n! Title: t\n||ads.example^\n/banner/\n@@||ok.example^\n##.ad\n");
    while (good.size() < 1024)
        good += QStringLiteral("||host%1.example^$script\n").arg(good.size());
    QVERIFY(FilterListManager::isPlausibleFilterList(good.toUtf8()));

    QVERIFY(!FilterListManager::isPlausibleFilterList(QByteArray()));
    QVERIFY(!FilterListManager::isPlausibleFilterList(QByteArray("too small")));
    // Binary garbage with invalid UTF-8.
    QByteArray garbage(2048, '\0');
    for (int i = 0; i < garbage.size(); ++i)
        garbage[i] = char((i * 31) & 0xFF);
    QVERIFY(!FilterListManager::isPlausibleFilterList(garbage));
    // HTML error page without any filter rules.
    QVERIFY(!FilterListManager::isPlausibleFilterList(QByteArray(2048, '<')));

    // REGRESSION: EasyPrivacy-style lists. EasyPrivacy is dominated by bare
    // URL-substring patterns ("-adobe-analytics/", ".beacon.min.js",
    // "&ev=PageView&") that carry no ||, ^, ## or $ tokens; an earlier
    // validator rejected the REAL list as "implausible content" and
    // silently dropped it. Such lines must count as rules.
    QString easyPrivacyStyle = QStringLiteral(
        "! EasyPrivacy-like\n"
        "&&sub19=undefined&sub20=undefined\n"
        "&ev=PageView&\n"
        "&hitType=pageview&\n"
        "-adobe-analytics/\n"
        "-didomi.js\n"
        "-geoIP.js\n"
        "-inview-tracker.\n"
        "-tracking-pixel.\n"
        ".beacon.min.js\n"
        ".com/_.gif?\n"
        ".com/clk?\n"
        "/adframe.\n"
        "/pageviews?\n"
        "/telemetry/beacon\n"
        "||analytics.example^\n"
        "||metrics.example^$script\n"
        "@@||firstparty.example^\n");
    while (easyPrivacyStyle.size() < 1024)
        easyPrivacyStyle += QStringLiteral("-tracker-%1.js\n").arg(easyPrivacyStyle.size());
    QVERIFY(FilterListManager::isPlausibleFilterList(easyPrivacyStyle.toUtf8()));

    // The REAL shipped fixtures (genuine EasyList + EasyPrivacy downloads)
    // must both validate, when present in the source tree.
    const QString fixturesDir = QStringLiteral(NOR_FIXTURES_DIR);
    for (const char *name : { "/filterlists/easylist.txt",
                              "/filterlists/easyprivacy.txt" }) {
        QFile f(fixturesDir + QLatin1String(name));
        if (!f.exists())
            continue;   // fixtures are optional at test time
        QVERIFY2(f.open(QIODevice::ReadOnly), name);
        const QByteArray raw = f.readAll();
        QVERIFY2(FilterListManager::isPlausibleFilterList(raw),
                 QStringLiteral("%1 (%2 bytes) rejected as implausible")
                     .arg(QLatin1String(name))
                     .arg(raw.size())
                     .toUtf8());
    }

    // Prose text (e.g. a fetched article) is not a filter list.
    QString prose = QStringLiteral(
        "This is a long block of ordinary prose text with spaces in every "
        "line, definitely not a filter list of any kind. ");
    while (prose.size() < 1024)
        prose += QStringLiteral("More sentences with spaces follow here. ");
    QVERIFY(!FilterListManager::isPlausibleFilterList(prose.toUtf8()));
}

void TestFilterLists::cacheFileNameIsStableAndSafe()
{
    const QString a = FilterListManager::cacheFileNameForUrl(
        QUrl("https://easylist.to/easylist/easylist.txt"));
    const QString b = FilterListManager::cacheFileNameForUrl(
        QUrl("https://easylist.to/easylist/easylist.txt"));
    const QString c = FilterListManager::cacheFileNameForUrl(
        QUrl("https://easylist.to/easylist/easyprivacy.txt"));
    QCOMPARE(a, b);
    QVERIFY(a != c);
    QVERIFY(a.endsWith(QStringLiteral(".txt")));
    QVERIFY(!a.contains(QLatin1Char('/')));
    QVERIFY(!a.contains(QStringLiteral("..")));
}

// ------------------------------------------------------------- downloads ----

void TestFilterLists::downloadGoodList()
{
    removeCachedLists();
    AppConfig config(m_dir.filePath("lists.ini"));
    config.setAdBlockListUrls({ m_baseUrl + "/filterlist/good" });
    config.setAdBlockAutoUpdate(true);
    AdBlockClient client(config);
    FilterListManager manager(config, client);
    manager.initialize();

    const QPair<int, int> result = runUpdate(manager);
    QCOMPARE(result.first, 1);    // one list changed
    QCOMPARE(result.second, 0);

    const QString file = Paths::filterListsDir() + QLatin1Char('/')
                         + FilterListManager::cacheFileNameForUrl(
                               QUrl(m_baseUrl + "/filterlist/good"));
    QFile cached(file);
    QVERIFY(cached.exists());
    QVERIFY(cached.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(cached.readAll());
    QVERIFY(text.contains("||tracker.example^"));

    // The engine was rebuilt and includes the downloaded rules.
    client.reload();
    QVERIFY(client.decide(QUrl("https://tracker.example/pixel"),
                          QUrl("https://news.example.org/"), 0).block);
    QVERIFY(!client.decide(QUrl("https://good.example/x"),
                           QUrl("https://other.example/"), 0).block);
}

void TestFilterLists::conditionalRequestReturns304()
{
    removeCachedLists();
    AppConfig config(m_dir.filePath("etag.ini"));
    config.setAdBlockListUrls({ m_baseUrl + "/filterlist/good" });
    AdBlockClient client(config);
    FilterListManager manager(config, client);
    manager.initialize();

    // First update stores the ETag and downloads the list.
    const QPair<int, int> first = runUpdate(manager);
    QCOMPARE(first.first, 1);
    // Second update: the server answers 304 (If-None-Match matched).
    const QPair<int, int> second = runUpdate(manager);
    QCOMPARE(second.first, 0);   // nothing changed
    QCOMPARE(second.second, 0);  // nothing failed
    QVERIFY(manager.warnings().isEmpty());
}

void TestFilterLists::corruptDownloadRejected()
{
    removeCachedLists();
    AppConfig config(m_dir.filePath("corrupt.ini"));
    config.setAdBlockListUrls({ m_baseUrl + "/filterlist/corrupt" });
    AdBlockClient client(config);
    FilterListManager manager(config, client);
    manager.initialize();

    const QPair<int, int> result = runUpdate(manager);
    QCOMPARE(result.first, 0);   // nothing replaced
    QCOMPARE(result.second, 1);  // one failure recorded

    // No cache file written for the corrupt download.
    const QString file = Paths::filterListsDir() + QLatin1Char('/')
                         + FilterListManager::cacheFileNameForUrl(
                               QUrl(m_baseUrl + "/filterlist/corrupt"));
    QVERIFY(!QFile::exists(file));
}

void TestFilterLists::htmlErrorPageRejected()
{
    removeCachedLists();
    AppConfig config(m_dir.filePath("html.ini"));
    config.setAdBlockListUrls({ m_baseUrl + "/filterlist/html-error" });
    AdBlockClient client(config);
    FilterListManager manager(config, client);
    manager.initialize();

    const QPair<int, int> result = runUpdate(manager);
    QCOMPARE(result.first, 0);
    QCOMPARE(result.second, 1);
}

void TestFilterLists::offlineFailureKeepsCache()
{
    // A dead endpoint configured alongside the good one: the failure must
    // never damage the working list's cached copy.
    removeCachedLists();
    AppConfig config(m_dir.filePath("offline.ini"));
    const QString goodUrl = m_baseUrl + "/filterlist/good";
    const QString deadUrl = QStringLiteral("http://127.0.0.1:9/list.txt");
    config.setAdBlockListUrls({ goodUrl, deadUrl });
    AdBlockClient client(config);
    FilterListManager manager(config, client);
    manager.initialize();

    const QPair<int, int> first = runUpdate(manager);
    QCOMPARE(first.first, 1);    // good list downloaded
    QCOMPARE(first.second, 1);   // dead endpoint failed

    const QString file = Paths::filterListsDir() + QLatin1Char('/')
                         + FilterListManager::cacheFileNameForUrl(QUrl(goodUrl));
    QVERIFY(QFile::exists(file));

    // Retry: the good list answers 304, the dead one fails again — the
    // cached copy must survive untouched.
    const QPair<int, int> second = runUpdate(manager);
    QCOMPARE(second.first, 0);
    QCOMPARE(second.second, 1);
    QVERIFY(QFile::exists(file));
    QFile cached(file);
    QVERIFY(cached.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(cached.readAll()).contains("||tracker.example^"));

    // And the engine still blocks with the cached rules (offline path in
    // AdBlockClient::reload reads every cached *.txt).
    client.reload();
    QVERIFY(client.decide(QUrl("https://tracker.example/pixel"),
                          QUrl("https://news.example.org/"), 0).block);
}

void TestFilterLists::staleUrlCacheRemoved()
{
    removeCachedLists();
    AppConfig config(m_dir.filePath("stale.ini"));
    config.setAdBlockListUrls({ m_baseUrl + "/filterlist/good" });
    AdBlockClient client(config);
    FilterListManager manager(config, client);
    manager.initialize();
    const QPair<int, int> first = runUpdate(manager);
    QCOMPARE(first.first, 1);
    const QString file = Paths::filterListsDir() + QLatin1Char('/')
                         + FilterListManager::cacheFileNameForUrl(
                               QUrl(m_baseUrl + "/filterlist/good"));
    QVERIFY(QFile::exists(file));

    // Removing the URL from the settings must remove its cached file.
    config.setAdBlockListUrls({});
    const QPair<int, int> second = runUpdate(manager);
    QCOMPARE(second.first, 1);   // removal counts as a change
    QVERIFY(!QFile::exists(file));
}

// ----------------------------------------------------------- engine cache ---

void TestFilterLists::engineCacheSpeedsUpStartup()
{
    removeCachedLists();
    AppConfig config(m_dir.filePath("cache.ini"));
    config.setAdBlockListUrls({ m_baseUrl + "/filterlist/good" });
    config.setCustomFilterLists({});
    AdBlockClient client(config);
    FilterListManager manager(config, client);

    // Download once so the engine has real rules to serialize.
    const QPair<int, int> dl = runUpdate(manager);
    QCOMPARE(dl.first, 1);

    QElapsedTimer parse;
    parse.start();
    manager.initialize();          // parses lists + saves cache
    const qint64 parseMs = parse.elapsed();
    QVERIFY(client.blockRuleCount() > 0);
    QVERIFY(QFile::exists(Paths::filterListsDir() + QStringLiteral("/engine.cache")));
    QVERIFY(QFile::exists(Paths::filterListsDir() + QStringLiteral("/engine.meta")));

    // Second manager on the same inputs must come from the cache.
    AdBlockClient client2(config);
    FilterListManager manager2(config, client2);
    QElapsedTimer cached;
    cached.start();
    manager2.initialize();
    const qint64 cacheMs = cached.elapsed();

    // Both engines must behave identically.
    const QUrl req("https://tracker.example/pixel");
    const QUrl first("https://news.example.org/");
    QCOMPARE(client2.decide(req, first, 0).block, client.decide(req, first, 0).block);
    QVERIFY(client2.decide(req, first, 0).block);

    qInfo("startup: parse %lld ms vs engine-cache %lld ms", parseMs, cacheMs);
    QVERIFY2(cacheMs <= parseMs,
             qPrintable(QString("cache %1 ms slower than parse %2 ms")
                            .arg(cacheMs).arg(parseMs)));
}

void TestFilterLists::corruptEngineCacheFallsBack()
{
    removeCachedLists();
    AppConfig config(m_dir.filePath("badcache.ini"));
    config.setAdBlockListUrls({ m_baseUrl + "/filterlist/good" });
    AdBlockClient client(config);
    FilterListManager manager(config, client);
    const QPair<int, int> dl = runUpdate(manager);
    QCOMPARE(dl.first, 1);
    manager.initialize();          // engine with rules → cache written
    const QString cachePath = Paths::filterListsDir() + QStringLiteral("/engine.cache");
    QVERIFY(QFile::exists(cachePath));

    // Corrupt the cache and the metadata separately.
    {
        QFile f(cachePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral("garbage!!"));
    }
    AdBlockClient client2(config);
    FilterListManager manager2(config, client2);
    manager2.initialize();          // must fall back to parsing, not crash
    // The corrupt cache was replaced with a fresh serialization.
    QFile f(cachePath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.size() > 16);

    // Corrupt metadata alone also falls back safely.
    {
        QFile meta(Paths::filterListsDir() + QStringLiteral("/engine.meta"));
        QVERIFY(meta.open(QIODevice::WriteOnly));
        meta.write("{ not json ]");
    }
    AdBlockClient client3(config);
    FilterListManager manager3(config, client3);
    manager3.initialize();
}

QTEST_MAIN(TestFilterLists)
#include "tst_filterlists.moc"
