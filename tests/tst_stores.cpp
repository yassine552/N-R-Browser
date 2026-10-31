// Unit tests for the SQLite stores (bookmarks + history) and settings
// persistence (AppConfig), plus storage integrity / recovery and the
// deferred site-storage wipe.
#include <QtTest>

#include "AppConfig.h"
#include "BookmarkStore.h"
#include "HistoryStore.h"
#include "Paths.h"
#include "RequestInterceptor.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

class TestStores : public QObject
{
    Q_OBJECT

private slots:
    // ---- bookmarks --------------------------------------------------------
    void bookmarksOpen();
    void bookmarksAdd();
    void bookmarksUpdate();
    void bookmarksRemove();
    void bookmarksContains();
    void bookmarksPersistAcrossReopen();

    // ---- history ----------------------------------------------------------
    void historyOpen();
    void historyAddUpserts();
    void historySearch();
    void historyRemove();
    void historyClear();
    void historyMostVisited();
    void historyTitleUpdate();
    void historyPersistAcrossReopen();

    // ---- config -----------------------------------------------------------
    void configDefaults();
    void configSetGetRoundtrip();
    void configPersistsAcrossReopen();
    void configPermissions();
    void configListRoundtrips();

    // ---- integrity / recovery ----------------------------------------------
    void historyCorruptFileRecovers();
    void bookmarksCorruptFileRecovers();
    void siteStorageWipeCompletes();
    void siteStorageWipeNoOpWhenNotPending();

    // ---- data-saver per-site policy ----------------------------------------
    void dataSaverImagePolicyPerSite();

private:
    QTemporaryDir m_dir;
};

// ------------------------------------------------------------ bookmarks ----

void TestStores::bookmarksOpen()
{
    BookmarkStore store(m_dir.filePath("bm1.db"));
    QVERIFY(store.isValid());
    QVERIFY(store.lastError().isEmpty());
}

void TestStores::bookmarksAdd()
{
    BookmarkStore store(m_dir.filePath("bm2.db"));
    QVERIFY(store.addBookmark("Qt", "https://qt.io"));
    QVERIFY(store.addBookmark("DuckDuckGo", "https://duckduckgo.com"));
    QCOMPARE(store.count(), qint64(2));
    // Duplicate URLs are allowed (folders) but each row is real.
    QVERIFY(store.addBookmark("Qt again", "https://qt.io", "work"));
    QCOMPARE(store.count(), qint64(3));
    // Empty URL rejected.
    QVERIFY(!store.addBookmark("broken", ""));
}

void TestStores::bookmarksUpdate()
{
    BookmarkStore store(m_dir.filePath("bm3.db"));
    store.addBookmark("Old", "https://old.example");
    const Bookmark b = store.bookmarkForUrl("https://old.example");
    QVERIFY(b.id > 0);
    QVERIFY(store.updateBookmark(b.id, "New", "https://new.example"));
    QVERIFY(!store.containsUrl("https://old.example"));
    QCOMPARE(store.bookmarkForUrl("https://new.example").title, QStringLiteral("New"));
    // Updating a nonexistent id fails cleanly.
    QVERIFY(!store.updateBookmark(999999, "x", "https://y.example"));
}

void TestStores::bookmarksRemove()
{
    BookmarkStore store(m_dir.filePath("bm4.db"));
    store.addBookmark("A", "https://a.example");
    const Bookmark b = store.bookmarkForUrl("https://a.example");
    QVERIFY(store.removeBookmark(b.id));
    QCOMPARE(store.count(), qint64(0));
    QVERIFY(store.removeBookmarkByUrl("https://never.added"));
}

void TestStores::bookmarksContains()
{
    BookmarkStore store(m_dir.filePath("bm5.db"));
    store.addBookmark("A", "https://a.example");
    QVERIFY(store.containsUrl("https://a.example"));
    QVERIFY(!store.containsUrl("https://b.example"));
    // Order follows insertion time.
    store.addBookmark("B", "https://b.example");
    const QVector<Bookmark> all = store.bookmarks();
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.first().url, QStringLiteral("https://a.example"));
}

void TestStores::bookmarksPersistAcrossReopen()
{
    const QString path = m_dir.filePath("bm6.db");
    {
        BookmarkStore store(path);
        store.addBookmark("Persistent", "https://qt.io");
    }
    BookmarkStore reopened(path);
    QCOMPARE(reopened.count(), qint64(1));
    QCOMPARE(reopened.bookmarkForUrl("https://qt.io").title, QStringLiteral("Persistent"));
}

// -------------------------------------------------------------- history ----

void TestStores::historyOpen()
{
    HistoryStore store(m_dir.filePath("h1.db"));
    QVERIFY(store.isValid());
}

void TestStores::historyAddUpserts()
{
    HistoryStore store(m_dir.filePath("h2.db"));
    QVERIFY(store.addVisit("https://qt.io", "Qt Framework"));
    QVERIFY(store.addVisit("https://qt.io", "Qt Framework — better title"));
    QCOMPARE(store.count(), qint64(1)); // upsert, not duplicate row
    const QVector<HistoryEntry> all = store.recent(10);
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().visitCount, 2);
    QCOMPARE(all.first().title, QStringLiteral("Qt Framework — better title"));
    QVERIFY(store.addVisit("https://example.org", "Example"));
    QCOMPARE(store.count(), qint64(2));
    // Most recent first.
    QCOMPARE(store.recent(10).first().url, QStringLiteral("https://example.org"));
}

void TestStores::historySearch()
{
    HistoryStore store(m_dir.filePath("h3.db"));
    store.addVisit("https://news.example.org/tech", "Tech News");
    store.addVisit("https://shop.example.net/gadgets", "Gadget Shop");
    store.addVisit("https://qt.io/docs", "Qt Documentation");
    // Title search.
    QCOMPARE(store.search("gadget").size(), 1);
    // URL search (substring in URL).
    QCOMPARE(store.search("example").size(), 2);
    // LIKE metacharacters are escaped: '%' is literal here.
    QCOMPARE(store.search("%").size(), 0);
    // Case-insensitive.
    QCOMPARE(store.search("TECH").size(), 1);
}

void TestStores::historyRemove()
{
    HistoryStore store(m_dir.filePath("h4.db"));
    store.addVisit("https://a.example", "A");
    store.addVisit("https://b.example", "B");
    const QVector<HistoryEntry> all = store.recent(10);
    QVERIFY(store.removeEntry(all.first().id));
    QCOMPARE(store.count(), qint64(1));
    QVERIFY(store.removeByUrl("https://a.example"));
    QCOMPARE(store.count(), qint64(0));
    QVERIFY(!store.removeEntry(999999));
}

void TestStores::historyClear()
{
    HistoryStore store(m_dir.filePath("h5.db"));
    store.addVisit("https://a.example", "A");
    store.addVisit("https://b.example", "B");
    QVERIFY(store.clearAll());
    QCOMPARE(store.count(), qint64(0));
}

void TestStores::historyMostVisited()
{
    HistoryStore store(m_dir.filePath("h6.db"));
    store.addVisit("https://a.example", "A");
    store.addVisit("https://a.example", "A");
    store.addVisit("https://a.example", "A");
    store.addVisit("https://b.example", "B");
    store.addVisit("https://b.example", "B");
    store.addVisit("https://c.example", "C"); // single visit -> excluded
    const QVector<HistoryEntry> top = store.mostVisited(5);
    QCOMPARE(top.size(), 2);
    QCOMPARE(top.first().url, QStringLiteral("https://a.example"));
}

void TestStores::historyTitleUpdate()
{
    HistoryStore store(m_dir.filePath("h7.db"));
    store.addVisit("https://slow.example", "Loading…");
    QVERIFY(store.updateTitleForUrl("https://slow.example", "Final Title"));
    QCOMPARE(store.recent(10).first().title, QStringLiteral("Final Title"));
}

void TestStores::historyPersistAcrossReopen()
{
    const QString path = m_dir.filePath("h8.db");
    {
        HistoryStore store(path);
        store.addVisit("https://qt.io", "Qt");
        store.addVisit("https://qt.io", "Qt");
    }
    HistoryStore reopened(path);
    QCOMPARE(reopened.count(), qint64(1));
    QCOMPARE(reopened.recent(10).first().visitCount, 2);
}

// --------------------------------------------------------------- config ----

void TestStores::configDefaults()
{
    AppConfig config(m_dir.filePath("c1.ini"));
    QCOMPARE(config.language(), QStringLiteral("en"));
    QVERIFY(config.adBlockEnabled());
    QVERIFY(!config.dataSaverEnabled());
    QVERIFY(config.askWhereToSaveDownloads());
    QVERIFY(config.bookmarksBarVisible());
    QCOMPARE(config.homePage(), QStringLiteral("nor://newtab"));
    QVERIFY(!config.downloadDirectory().isEmpty());
}

void TestStores::configSetGetRoundtrip()
{
    AppConfig config(m_dir.filePath("c2.ini"));
    config.setLanguage("ar");
    config.setHomePage("https://example.org");
    config.setDataSaverEnabled(true);
    config.setAdBlockEnabled(false);
    config.setDownloadDirectory("/tmp/downloads");
    QCOMPARE(config.language(), QStringLiteral("ar"));
    QCOMPARE(config.homePage(), QStringLiteral("https://example.org"));
    QVERIFY(config.dataSaverEnabled());
    QVERIFY(!config.adBlockEnabled());
    QCOMPARE(config.downloadDirectory(), QStringLiteral("/tmp/downloads"));
}

void TestStores::configPersistsAcrossReopen()
{
    const QString path = m_dir.filePath("c3.ini");
    {
        AppConfig config(path);
        config.setLanguage("fr");
        config.setCustomFilterLists({ "/tmp/a.txt", "/tmp/b.txt" });
        config.setAdBlockAllowlist({ "example.com" });
    }
    AppConfig reopened(path);
    QCOMPARE(reopened.language(), QStringLiteral("fr"));
    QCOMPARE(reopened.customFilterLists(),
             QStringList({ "/tmp/a.txt", "/tmp/b.txt" }));
    QCOMPARE(reopened.adBlockAllowlist(), QStringList({ "example.com" }));
}

void TestStores::configPermissions()
{
    AppConfig config(m_dir.filePath("c4.ini"));
    QVERIFY(config.permission("https://maps.example", "geolocation").isEmpty());
    config.setPermission("https://maps.example", "geolocation", "allow");
    config.setPermission("https://maps.example", "notifications", "deny");
    QCOMPARE(config.permission("https://maps.example", "geolocation"), QStringLiteral("allow"));
    QCOMPARE(config.permission("https://maps.example", "notifications"), QStringLiteral("deny"));
    QCOMPARE(config.permissionOrigins(), QStringList({ "https://maps.example" }));
    config.removePermission("https://maps.example");
    QVERIFY(config.permissionOrigins().isEmpty());
    QVERIFY(config.permission("https://maps.example", "geolocation").isEmpty());
}

void TestStores::configListRoundtrips()
{
    AppConfig config(m_dir.filePath("c5.ini"));
    config.setDataSaverExceptions({ "a.example", "b.example" });
    QCOMPARE(config.dataSaverExceptions(),
             QStringList({ "a.example", "b.example" }));
    const QString json = QStringLiteral("[{\"name\":\"Test\",\"url\":\"https://t.example/?q=%s\"}]");
    config.setSearchEnginesJson(json);
    QCOMPARE(config.searchEnginesJson(), json);
    config.setDefaultSearchEngine("Test");
    QCOMPARE(config.defaultSearchEngine(), QStringLiteral("Test"));
}

QTEST_MAIN(TestStores)
#include "tst_stores.moc"

// ------------------------------------------------- integrity / recovery ----

void TestStores::historyCorruptFileRecovers()
{
    // A corrupt database file must not crash or hang the browser: the
    // store reports invalid, the error is surfaced, and a fresh write
    // after re-creation recovers a working store.
    const QString path = m_dir.filePath("corrupt-history.db");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral("this is definitely not a sqlite file"));
    }
    {
        HistoryStore store(path);
        QVERIFY(!store.isValid());
        QVERIFY(!store.lastError().isEmpty());
        QCOMPARE(store.count(), qint64(0));
        QVERIFY(store.recent().isEmpty());
    }
    // Re-opening after the corrupted file was replaced recovers.
    QFile::remove(path);
    {
        HistoryStore store(path);
        QVERIFY(store.isValid());
        QVERIFY(store.addVisit("https://recovered.example.org/", "Recovered"));
        QCOMPARE(store.count(), qint64(1));
    }
}

void TestStores::bookmarksCorruptFileRecovers()
{
    const QString path = m_dir.filePath("corrupt-bookmarks.db");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(2048, '\xFF'));
    }
    BookmarkStore store(path);
    QVERIFY(!store.isValid());
    QVERIFY(store.bookmarks().isEmpty());
    // The store stays safe to call.
    QVERIFY(!store.containsUrl("https://x.example.org/"));
}

void TestStores::siteStorageWipeCompletes()
{
    // The deferred wipe must actually remove the WebEngine storage tree
    // and clear the pending flag — "cleared" is only claimed once the
    // deletion completed.
    Paths::setPendingClearSiteStorage(true);
    const QString dir = Paths::webEngineDir();
    QDir(dir).mkpath(dir + QStringLiteral("/regular/Local Storage"));
    QFile marker(dir + QStringLiteral("/regular/Local Storage/leveldb-marker"));
    {
        QFile f(marker.fileName());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    QVERIFY(QFile::exists(marker.fileName()));

    QVERIFY(Paths::applyPendingSiteStorageWipe());
    QVERIFY(!QFile::exists(marker.fileName()));
    QVERIFY(!QDir(dir + QStringLiteral("/regular")).exists());
    QVERIFY(!Paths::pendingClearSiteStorage());
}

void TestStores::siteStorageWipeNoOpWhenNotPending()
{
    Paths::setPendingClearSiteStorage(false);
    const QString dir = Paths::webEngineDir();
    QDir(dir).mkpath(dir + QStringLiteral("/untouched"));
    QFile keeper(dir + QStringLiteral("/untouched/keep.txt"));
    {
        QFile f(keeper.fileName());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("keep");
    }
    QVERIFY(!Paths::applyPendingSiteStorageWipe());
    QVERIFY(QFile::exists(keeper.fileName()));
}

// ------------------------------------------------ data-saver per-site ------

void TestStores::dataSaverImagePolicyPerSite()
{
    using RI = RequestInterceptor;
    const QUrl firstParty = QUrl("https://news.example.org/page");
    const QUrl sameSiteImage = QUrl("https://news.example.org/img/logo.png");
    const QUrl thirdPartyImage = QUrl("https://cdn.other.net/img/ad.png");

    // Block-all mode (default): every image on non-excepted sites.
    QVERIFY(RI::dataSaverBlocksImage(sameSiteImage, firstParty,
                                     true, false, true, true));
    QVERIFY(RI::dataSaverBlocksImage(thirdPartyImage, firstParty,
                                     true, false, true, true));
    // Excepted sites never have images blocked (v1.0.0 limitation #3 fixed).
    QVERIFY(!RI::dataSaverBlocksImage(sameSiteImage, firstParty,
                                      true, true, true, true));
    QVERIFY(!RI::dataSaverBlocksImage(thirdPartyImage, firstParty,
                                      true, true, true, true));
    // Third-party-only mode: first-party images load.
    QVERIFY(!RI::dataSaverBlocksImage(sameSiteImage, firstParty,
                                      true, false, false, true));
    QVERIFY(RI::dataSaverBlocksImage(thirdPartyImage, firstParty,
                                     true, false, false, true));
    // Data saver off: nothing blocked.
    QVERIFY(!RI::dataSaverBlocksImage(sameSiteImage, firstParty,
                                      false, false, true, true));
    QVERIFY(!RI::dataSaverBlocksImage(thirdPartyImage, firstParty,
                                      false, false, true, true));
}
