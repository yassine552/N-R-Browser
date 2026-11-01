// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
// Unit tests for the Firefox-style Top Sites system: frecency ranking,
// host grouping, pin/unpin/remove/edit semantics, state persistence,
// history-clear behaviour, and the local favicon cache (including the
// hostile-host sanitization that guards the cache directory).
#include <QtTest>

#include "FaviconStore.h"
#include "HistoryStore.h"
#include "TopSitesStore.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QTemporaryDir>

class TestTopSites : public QObject
{
    Q_OBJECT

private slots:
    // Runs before EVERY test function: fresh scratch dir.
    void init();

    // ---- ranking ----------------------------------------------------------
    void frecencyScoreDecays();
    void frecencyScoreCases();
    void baseDomainOfCases();
    void rankingFrequencyAndRecency();
    void rankingHostGrouping();
    void rankingSubdomainsMergeIntoBase();
    void rankingManyPathsOneTile();
    void rankingStaleSiteSinks();
    void rankingExcludesNonHttp();
    void limitRespected();

    // ---- search-engine exclusion ------------------------------------------
    void searchExclusionHidesDerivedTiles();
    void searchExclusionIsPatternBased();
    void searchExclusionKeepsPins();
    void isEnginePageUrlCases();

    // ---- state self-healing ------------------------------------------------
    void loadSelfHealsDuplicatePins();

    // ---- pins / removals / edits ------------------------------------------
    void pinSurvivesRankingAndClear();
    void pinIdempotentTitleUpdate();
    void unpinReturnsToOrganic();
    void removeDerivedHidesWholeHost();
    void removePinHidesByUrl();
    void editDerivedBecomesPin();
    void editReplacesExistingPin();

    // ---- persistence -------------------------------------------------------
    void statePersistsAcrossReopen();
    void corruptStateStartsFresh();
    void clearHistoryWipesDerivedButKeepsPins();

    // ---- favicon cache -----------------------------------------------------
    void faviconCaptureAndRead();
    void faviconSanitizeHost();
    void faviconMissingIsEmpty();
    void faviconClearAll();

private:
    QTemporaryDir m_dir;
    QString m_sub;
    int m_run = 0;
    HistoryStore *makeHistory();
    QString statePath() const;
    // N visits of `url` at a fixed point in time (the browser itself
    // always records "now"; tests build age profiles instead).
    static void seed(HistoryStore *h, const char *url, const char *title,
                     int visits, qint64 when)
    {
        for (int i = 0; i < visits; ++i)
            h->addVisit(QString::fromLatin1(url), QString::fromLatin1(title), when);
    }
};

void TestTopSites::init()
{
    m_sub = QStringLiteral("/run%1").arg(++m_run);
    QDir().mkpath(m_dir.path() + m_sub);
}

HistoryStore *TestTopSites::makeHistory()
{
    return new HistoryStore(m_dir.path() + m_sub + QStringLiteral("/history.db"));
}

QString TestTopSites::statePath() const
{
    return m_dir.path() + m_sub + QStringLiteral("/topsites.json");
}

// ------------------------------------------------------------ ranking ----

void TestTopSites::frecencyScoreDecays()
{
    const qint64 now = 1'000'000'000;
    const qreal fresh = TopSitesStore::frecencyScore(10, now, now);
    const qreal half = TopSitesStore::frecencyScore(10, now - 14 * 86400, now);
    const qreal quarter = TopSitesStore::frecencyScore(10, now - 28 * 86400, now);
    // 14-day half-life: each two weeks halves the weight.
    QVERIFY(fresh > half && half > quarter);
    QVERIFY(qAbs(half - fresh / 2) < 0.01);
    QVERIFY(qAbs(quarter - fresh / 4) < 0.01);
    // Clock skew after the fact is clamped, never negative.
    QVERIFY(TopSitesStore::frecencyScore(5, now + 86400, now) >= 5.0);
    QVERIFY(TopSitesStore::frecencyScore(0, now, now) == 0.0);
}

void TestTopSites::frecencyScoreCases()
{
    const qint64 now = 2'000'000'000;
    // A heavily-visited-but-old site must lose against a few recent visits.
    const qreal oldHeavy = TopSitesStore::frecencyScore(50, now - 300 * 86400, now);
    const qreal recentFew = TopSitesStore::frecencyScore(4, now - 86400, now);
    QVERIFY(recentFew > oldHeavy);
}

void TestTopSites::baseDomainOfCases()
{
    // The registrable-domain grouping key (user-facing rule: ONE tile per
    // site — www/mobile variants of the same site never duplicate).
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("www.youtube.com")),
             QStringLiteral("youtube.com"));
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("youtube.com")),
             QStringLiteral("youtube.com"));
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("m.youtube.com")),
             QStringLiteral("youtube.com"));
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("www2.example.org")),
             QStringLiteral("example.org"));
    // Two-level public suffixes: the base is the last THREE labels.
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("news.bbc.co.uk")),
             QStringLiteral("bbc.co.uk"));
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("www.bbc.co.uk")),
             QStringLiteral("bbc.co.uk"));
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("site.tn")),
             QStringLiteral("site.tn"));
    // Plain two-label domains stay themselves.
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("7-zip.org")),
             QStringLiteral("7-zip.org"));
    // Single labels and IPs pass through unchanged.
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("localhost")),
             QStringLiteral("localhost"));
    QCOMPARE(TopSitesStore::baseDomainOf(QStringLiteral("192.168.1.10")),
             QStringLiteral("192.168.1.10"));
    QCOMPARE(TopSitesStore::baseDomainOf(QString()), QString());
}

void TestTopSites::rankingFrequencyAndRecency()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // Frequency can beat recency...
    seed(h.data(), "https://q.example/", "Q", 30, now - 8 * 86400);
    // ...and recency can beat frequency...
    seed(h.data(), "https://p.example/", "P", 6, now - 86400);
    // ...but a stale heavyweight sinks below both.
    seed(h.data(), "https://r.example/", "R", 25, now - 60 * 86400);
    TopSitesStore store(h.data(), statePath());
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 3);
    QCOMPARE(top.at(0).host, QStringLiteral("q.example"));
    QCOMPARE(top.at(1).host, QStringLiteral("p.example"));
    QCOMPARE(top.at(2).host, QStringLiteral("r.example"));
    QVERIFY(!top.at(0).pinned);
}

void TestTopSites::rankingHostGrouping()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // Same host, several URLs: one tile, best representative.
    seed(h.data(), "https://github.com/", "GitHub", 10, now - 86400);
    seed(h.data(), "https://github.com/nor", "NOR Repo", 10, now - 86400);
    seed(h.data(), "https://gitlab.com/", "GitLab", 6, now - 86400);
    TopSitesStore store(h.data(), statePath());
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 2);
    int githubTiles = 0;
    for (const TopSite &s : top) {
        if (s.baseDomain == QLatin1String("github.com"))
            ++githubTiles;
    }
    QCOMPARE(githubTiles, 1);
    // Ties prefer the shorter path: github.com/ represents the site.
    const auto it = std::find_if(top.cbegin(), top.cend(),
                                 [](const TopSite &s) { return s.baseDomain == QLatin1String("github.com"); });
    QVERIFY(it != top.cend());
    QCOMPARE(it->url, QStringLiteral("https://github.com/"));
}

void TestTopSites::rankingSubdomainsMergeIntoBase()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // THE duplicate bug this fixes: the same site reached through www.,
    // bare and m. hosts used to occupy THREE tiles.
    seed(h.data(), "https://www.7-zip.org/", "7-Zip", 4, now - 86400);
    seed(h.data(), "https://7-zip.org/download", "7-Zip Download", 4, now - 86400);
    seed(h.data(), "https://m.7-zip.org/", "7-Zip", 2, now - 86400);
    // A different site stays separate.
    seed(h.data(), "https://example.com/", "Example", 3, now - 86400);
    TopSitesStore store(h.data(), statePath());
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 2);
    const auto z = std::find_if(top.cbegin(), top.cend(),
                                [](const TopSite &s) { return s.baseDomain == QLatin1String("7-zip.org"); });
    QVERIFY(z != top.cend());
    // Best representative: highest score, shorter path on ties.
    QCOMPARE(z->url, QStringLiteral("https://www.7-zip.org/"));
    // Cross-subdomain scores ADD UP (one strong site, not three weak).
    QVERIFY(z->score > 4.0);
    // The favicon fallback chain carries every host seen under the base.
    QVERIFY(z->faviconHosts.contains(QStringLiteral("www.7-zip.org")));
    QVERIFY(z->faviconHosts.contains(QStringLiteral("7-zip.org")));
    QVERIFY(z->faviconHosts.contains(QStringLiteral("m.7-zip.org")));
    QVERIFY(z->faviconHosts.size() >= 3);
}

void TestTopSites::rankingManyPathsOneTile()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // THE user-reported duplicate: one service reached through MANY links
    // (test runs, direct links, subpages) used to fill several tiles with
    // the same site. One base domain => exactly one tile, whatever the
    // number of distinct URLs.
    const char *cvt[] = {
        "https://coveryourtracks.eff.org/",
        "https://coveryourtracks.eff.org/about",
        "https://coveryourtracks.eff.org/learn",
        "https://coveryourtracks.eff.org/what-is-fingerprinting",
        "https://coveryourtracks.eff.org/test",
        "https://coveryourtracks.eff.org/results",
        "https://coveryourtracks.eff.org/faq",
    };
    for (const char *u : cvt)
        seed(h.data(), u, "Cover Your Tracks", 3, now - 86400);
    // Same host again through a www. spelling, plus one unrelated site.
    seed(h.data(), "https://www.7-zip.org/", "7-Zip", 4, now - 86400);
    seed(h.data(), "https://example.com/", "Example", 2, now - 86400);
    TopSitesStore store(h.data(), statePath());
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 3);
    int cvtTiles = 0, zipTiles = 0;
    for (const TopSite &s : top) {
        if (s.baseDomain == QLatin1String("eff.org"))
            ++cvtTiles;
        if (s.baseDomain == QLatin1String("7-zip.org"))
            ++zipTiles;
    }
    QCOMPARE(cvtTiles, 1);   // seven URLs, ONE tile
    QCOMPARE(zipTiles, 1);
    // All the visit weight still counts: the merged tile outranks the
    // 4-visit 7-Zip tile (21 decayed visits vs 4).
    const auto it = std::find_if(top.cbegin(), top.cend(),
                                 [](const TopSite &s) { return s.baseDomain == QLatin1String("eff.org"); });
    QVERIFY(it != top.cend());
    QVERIFY(it->score > 4.0);
}

void TestTopSites::rankingStaleSiteSinks()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://stale.example/", "Stale", 200, now - 400 * 86400);
    seed(h.data(), "https://fresh.example/", "Fresh", 2, now - 86400);
    TopSitesStore store(h.data(), statePath());
    // Below the derived-score floor the stale site loses its slot even
    // with plenty of room: "most visited", not "least forgotten".
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 1);
    QCOMPARE(top.at(0).host, QStringLiteral("fresh.example"));
}

void TestTopSites::rankingExcludesNonHttp()
{
    TopSitesStore store(nullptr, statePath());
    QCOMPARE(TopSitesStore::hostOf(QStringLiteral("nor://newtab")), QString());
    QCOMPARE(TopSitesStore::hostOf(QStringLiteral("about:blank")), QString());
    QCOMPARE(TopSitesStore::hostOf(QStringLiteral("file:///etc/passwd")), QString());
    QCOMPARE(TopSitesStore::hostOf(QStringLiteral("https://Example.COM/x")),
             QStringLiteral("example.com"));
}

void TestTopSites::limitRespected()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (int i = 0; i < 20; ++i)
        h->addVisit(QStringLiteral("https://site%1.example/").arg(i), QStringLiteral("S"),
                    now - i * 86400);
    TopSitesStore store(h.data(), statePath());
    QCOMPARE(store.topSites(6).size(), 6);
}

// ------------------------------------------- search-engine exclusion ----

void TestTopSites::searchExclusionHidesDerivedTiles()
{
    // The pattern from the user's report: DuckDuckGo result pages for the
    // queries used to FIND the sites, with the sites themselves visited
    // only via those searches. The engine must never take a derived slot.
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://duckduckgo.com/?q=browserleaks", "browserleaks at DuckDuckGo",
         8, now - 3600);
    seed(h.data(), "https://duckduckgo.com/?q=https%3A%2F%2Fiphey.com%2F",
         "https://iphey.com/ at DuckDuckGo", 6, now - 3500);
    seed(h.data(), "https://browserleaks.com/",
         "BrowserLeaks - Check your browser for privacy leaks", 3, now - 3000);
    seed(h.data(), "https://iphey.com/", "Iphey", 2, now - 2900);
    TopSitesStore store(h.data(), statePath());

    // Before the exclusion is configured, the engine CAN rank (it is the
    // most visited host — exactly the bug).
    {
        const QVector<TopSite> top = store.topSites(10);
        QVERIFY(top.size() >= 3);
        QCOMPARE(top.at(0).baseDomain, QStringLiteral("duckduckgo.com"));
    }

    store.setEngineUrlTemplates(
        QStringList{ QStringLiteral("https://duckduckgo.com/?q=%s") });
    const QVector<TopSite> top = store.topSites(10);
    for (const TopSite &s : top)
        QVERIFY(s.baseDomain != QStringLiteral("duckduckgo.com"));
    QCOMPARE(top.size(), 2); // the two real sites, nothing else
    QCOMPARE(top.at(0).host, QStringLiteral("browserleaks.com"));
    QCOMPARE(top.at(1).host, QStringLiteral("iphey.com"));
}

void TestTopSites::searchExclusionIsPatternBased()
{
    // A content site that doubles as an engine (Wikipedia) only loses its
    // SEARCH pages — articles survive, and the engine page never becomes
    // the representative URL of the tile.
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://en.wikipedia.org/w/index.php?search=privacy",
         "privacy - Search - Wikipedia", 7, now - 3600);
    seed(h.data(), "https://en.wikipedia.org/wiki/Privacy", "Privacy - Wikipedia", 5, now - 3400);
    seed(h.data(), "https://www.youtube.com/", "YouTube", 2, now - 3300);
    TopSitesStore store(h.data(), statePath());
    store.setEngineUrlTemplates(QStringList{
        QStringLiteral("https://duckduckgo.com/?q=%s"),
        QStringLiteral("https://en.wikipedia.org/w/index.php?search=%s"),
        QStringLiteral("https://www.google.com/search?q=%s"),
    });
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 2);
    QCOMPARE(top.at(0).host, QStringLiteral("en.wikipedia.org"));
    // The representative URL is the ARTICLE, not the search page.
    QCOMPARE(top.at(0).url, QStringLiteral("https://en.wikipedia.org/wiki/Privacy"));
    QCOMPARE(top.at(1).host, QStringLiteral("www.youtube.com"));
}

void TestTopSites::searchExclusionKeepsPins()
{
    // A pinned engine page is an explicit user action — it stays.
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://duckduckgo.com/?q=x", "x at DuckDuckGo", 9, now - 60);
    TopSitesStore store(h.data(), statePath());
    store.setEngineUrlTemplates(
        QStringList{ QStringLiteral("https://duckduckgo.com/?q=%s") });
    QCOMPARE(store.topSites(10).size(), 0); // derived: hidden
    QVERIFY(store.pinSite(QUrl(QStringLiteral("https://duckduckgo.com/")),
                          QStringLiteral("DuckDuckGo")));
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 1);
    QVERIFY(top.at(0).pinned);
    QCOMPARE(top.at(0).host, QStringLiteral("duckduckgo.com"));
}

void TestTopSites::isEnginePageUrlCases()
{
    using TSS = TopSitesStore;
    const QStringList engines{
        QStringLiteral("https://duckduckgo.com/?q=%s"),
        QStringLiteral("https://www.google.com/search?q=%s"),
        QStringLiteral("https://en.wikipedia.org/w/index.php?search=%s"),
    };
    QVERIFY(TSS::isEnginePageUrl("https://duckduckgo.com/?q=browserleaks", engines));
    QVERIFY(TSS::isEnginePageUrl("https://www.google.com/search?q=privacy", engines));
    QVERIFY(TSS::isEnginePageUrl("https://google.com/search?q=privacy", engines)); // www-insensitive
    QVERIFY(TSS::isEnginePageUrl(
        "https://en.wikipedia.org/w/index.php?search=privacy", engines));
    QVERIFY(!TSS::isEnginePageUrl("https://en.wikipedia.org/wiki/Privacy", engines));
    QVERIFY(!TSS::isEnginePageUrl("https://duckduckgo.com/", engines)); // engine home: a real destination
    QVERIFY(!TSS::isEnginePageUrl("https://browserleaks.com/", engines));
    QVERIFY(!TSS::isEnginePageUrl("https://duckduckgo.com/?q=a", QStringList()));
    QVERIFY(!TSS::isEnginePageUrl("about:blank", engines));
    QVERIFY(!TSS::isEnginePageUrl(QString(), engines));
}

// ------------------------------------------------ state self-healing ----

void TestTopSites::loadSelfHealsDuplicatePins()
{
    // State written by an OLDER build (per-host grouping): three pins for
    // what is ONE site. Loading collapses them; the first pin wins.
    const QString path = statePath();
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({
  "pinned": [
    {"url": "https://coveryourtracks.eff.org/", "title": "Cover Your Tracks"},
    {"url": "https://panopticlick.eff.org/", "title": "Cover Your Tracks"},
    {"url": "https://www.coveryourtracks.eff.org/", "title": "Cover Your Tracks"},
    {"url": "https://github.com/", "title": "GitHub"}
  ],
  "removedHosts": [],
  "removedUrls": []
})");
    }
    QScopedPointer<HistoryStore> h(makeHistory());
    TopSitesStore store(h.data(), path);
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 2);
    QCOMPARE(top.at(0).host, QStringLiteral("coveryourtracks.eff.org"));
    QCOMPARE(top.at(1).host, QStringLiteral("github.com"));
    // ...and the healing is persisted for the next start.
    TopSitesStore reopened(h.data(), path);
    QCOMPARE(reopened.topSites(10).size(), 2);
}

// ---------------------------------------------------- pins / edits ----

void TestTopSites::pinSurvivesRankingAndClear()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://a.example/", "A", 5, now - 86400);
    TopSitesStore store(h.data(), statePath());
    QVERIFY(store.pinSite(QUrl(QStringLiteral("https://pinned.example/")), QStringLiteral("Pinned")));
    // Pin first regardless of ranking.
    QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.at(0).host, QStringLiteral("pinned.example"));
    QVERIFY(top.at(0).pinned);
    // ...and it survives a history clear, like a bookmark.
    h->clearAll();
    top = store.topSites(10);
    QCOMPARE(top.size(), 1);
    QCOMPARE(top.at(0).url, QStringLiteral("https://pinned.example/"));
    QCOMPARE(top.at(0).title, QStringLiteral("Pinned"));
}

void TestTopSites::pinIdempotentTitleUpdate()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    TopSitesStore store(h.data(), statePath());
    const QUrl u(QStringLiteral("https://x.example/"));
    QVERIFY(store.pinSite(u, QStringLiteral("One")));
    QVERIFY(store.pinSite(u, QStringLiteral("Two"))); // update, not duplicate
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 1);
    QCOMPARE(top.at(0).title, QStringLiteral("Two"));
}

void TestTopSites::unpinReturnsToOrganic()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://a.example/", "A", 5, now - 86400);
    TopSitesStore store(h.data(), statePath());
    const QUrl u(QStringLiteral("https://a.example/"));
    store.pinSite(u, QStringLiteral("A"));
    QVERIFY(store.isPinned(u));
    QVERIFY(store.unpinSite(u));
    QVERIFY(!store.isPinned(u));
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 1);
    QVERIFY(!top.at(0).pinned);
}

void TestTopSites::removeDerivedHidesWholeHost()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://a.example/", "A", 5, now - 86400);
    seed(h.data(), "https://a.example/other", "A2", 5, now - 86400);
    seed(h.data(), "https://b.example/", "B", 4, now - 86400);
    TopSitesStore store(h.data(), statePath());
    QVERIFY(store.removeSite(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("A")));
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 1); // only b.example: the whole host is hidden
    QCOMPARE(top.at(0).host, QStringLiteral("b.example"));
}

void TestTopSites::removePinHidesByUrl()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://a.example/", "A", 5, now - 86400);
    TopSitesStore store(h.data(), statePath());
    store.pinSite(QUrl(QStringLiteral("https://p.example/")), QStringLiteral("P"));
    store.pinSite(QUrl(QStringLiteral("https://q.example/")), QStringLiteral("Q"));
    QVERIFY(store.removeSite(QUrl(QStringLiteral("https://p.example/")), QStringLiteral("P")));
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 2); // q.example (pin) + a.example (history)
    for (const TopSite &s : top)
        QVERIFY(s.host != QLatin1String("p.example"));
    // Re-pin over a dismissed URL is still possible (explicit user action).
    QVERIFY(store.pinSite(QUrl(QStringLiteral("https://p.example/")), QStringLiteral("P2")));
    QVERIFY(store.isPinned(QUrl(QStringLiteral("https://p.example/"))));
}

void TestTopSites::editDerivedBecomesPin()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://old.example/", "Old Title", 5, now - 86400);
    TopSitesStore store(h.data(), statePath());
    QVERIFY(store.editSite(QUrl(QStringLiteral("https://old.example/")),
                           QUrl(QStringLiteral("https://new.example/")),
                           QStringLiteral("New Title")));
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 1);
    QVERIFY(top.at(0).pinned);
    QCOMPARE(top.at(0).url, QStringLiteral("https://new.example/"));
    QCOMPARE(top.at(0).title, QStringLiteral("New Title"));
}

void TestTopSites::editReplacesExistingPin()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    TopSitesStore store(h.data(), statePath());
    store.pinSite(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("A"));
    QVERIFY(store.editSite(QUrl(QStringLiteral("https://a.example/")),
                           QUrl(QStringLiteral("https://b.example/")),
                           QStringLiteral("B")));
    const QVector<TopSite> top = store.topSites(10);
    QCOMPARE(top.size(), 1);
    QCOMPARE(top.at(0).url, QStringLiteral("https://b.example/"));
}

// ---------------------------------------------------- persistence ----

void TestTopSites::statePersistsAcrossReopen()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://a.example/", "A", 5, now - 86400);
    {
        TopSitesStore store(h.data(), statePath());
        store.pinSite(QUrl(QStringLiteral("https://p.example/")), QStringLiteral("P"));
        store.removeSite(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("A"));
    }
    TopSitesStore reopened(h.data(), statePath());
    const QVector<TopSite> top = reopened.topSites(10);
    QCOMPARE(top.size(), 1);
    QCOMPARE(top.at(0).url, QStringLiteral("https://p.example/"));
}

void TestTopSites::corruptStateStartsFresh()
{
    QFile f(statePath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"pinned\": [ this is not json ");
    f.close();
    QScopedPointer<HistoryStore> h(makeHistory());
    TopSitesStore store(h.data(), statePath());
    QCOMPARE(store.topSites(10).size(), 0); // no crash, empty state
    // And the store still works afterwards.
    QVERIFY(store.pinSite(QUrl(QStringLiteral("https://ok.example/")), QStringLiteral("OK")));
    QCOMPARE(store.topSites(10).size(), 1);
}

void TestTopSites::clearHistoryWipesDerivedButKeepsPins()
{
    QScopedPointer<HistoryStore> h(makeHistory());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    seed(h.data(), "https://a.example/", "A", 5, now - 86400);
    TopSitesStore store(h.data(), statePath());
    store.pinSite(QUrl(QStringLiteral("https://p.example/")), QStringLiteral("P"));
    store.removeSite(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("A"));
    h->clearAll();
    // Dismissal list is history-derived data: gone with the history.
    seed(h.data(), "https://a.example/", "A", 2, now - 86400);
    const QVector<TopSite> top = store.topSites(10);
    int pins = 0, derived = 0;
    for (const TopSite &s : top)
        s.pinned ? ++pins : ++derived;
    QCOMPARE(pins, 1);
    QCOMPARE(derived, 1); // a.example is back: its dismissal was wiped
}

// ---------------------------------------------------- favicon cache ----

static QIcon makeColoredIcon(int rgb)
{
    QPixmap pm(64, 64);
    pm.fill(QColor(rgb));
    return QIcon(pm);
}

void TestTopSites::faviconCaptureAndRead()
{
    FaviconStore store(m_dir.path() + QStringLiteral("/favicons"));
    store.capture(makeColoredIcon(0x8b5cf6), QUrl(QStringLiteral("https://github.com/")));
    QVERIFY(store.hasIcon(QStringLiteral("github.com")));
    const QByteArray png = store.iconPng(QStringLiteral("github.com"));
    QVERIFY(!png.isEmpty());
    QVERIFY(png.startsWith(QByteArrayLiteral("\x89PNG")));

    // Reading it back gives the pixels we saved — NORMALIZED: every icon
    // is stored on one 36x36 canvas with the (aspect-preserved) content
    // centered inside a 30px box, so all tiles share one optical size
    // regardless of the source favicon's dimensions.
    QPixmap pm;
    QVERIFY(pm.loadFromData(png, "PNG"));
    QCOMPARE(pm.width(), 36);
    QCOMPARE(pm.height(), 36);
    const QImage img = pm.toImage();
    // Center pixel: the icon itself.
    QCOMPARE(qRed(img.pixel(18, 18)), 0x8b);
    // Corner pixel: transparent padding of the normalized canvas.
    QCOMPARE(qAlpha(img.pixel(1, 1)), 0);
    // Content box: opaque at its edge, empty outside it (30x30 centered).
    QVERIFY(qAlpha(img.pixel(3, 18)) > 0);   // x = (36-30)/2 .. just inside
    QCOMPARE(qAlpha(img.pixel(2, 18)), 0);   // just outside
}

void TestTopSites::faviconSanitizeHost()
{
    QCOMPARE(FaviconStore::sanitizeHost(QStringLiteral("GitHub.COM")),
             QStringLiteral("github.com"));
    // Traversal and hostile characters are refused outright.
    QVERIFY(FaviconStore::sanitizeHost(QStringLiteral("../../etc")).isEmpty());
    QVERIFY(FaviconStore::sanitizeHost(QStringLiteral("..")).isEmpty());
    QVERIFY(FaviconStore::sanitizeHost(QStringLiteral("a/b")).isEmpty());
    QVERIFY(FaviconStore::sanitizeHost(QStringLiteral("a b")).isEmpty());
    QVERIFY(FaviconStore::sanitizeHost(QString()).isEmpty());
    QVERIFY(FaviconStore::sanitizeHost(QStringLiteral("ok.example")).size() > 0);
    // The derived URL keeps the sanitized host.
    QCOMPARE(FaviconStore::iconUrlFor(QStringLiteral("example.com")),
             QStringLiteral("nor://favicon/example.com"));
}

void TestTopSites::faviconMissingIsEmpty()
{
    FaviconStore store(m_dir.path() + QStringLiteral("/favicons-missing"));
    QVERIFY(!store.hasIcon(QStringLiteral("never-visited.example")));
    QVERIFY(store.iconPng(QStringLiteral("never-visited.example")).isEmpty());
    // Null icons / bad URLs are silent no-ops.
    store.capture(QIcon(), QUrl(QStringLiteral("https://x.example/")));
    store.capture(makeColoredIcon(0x123456), QUrl(QStringLiteral("nor://newtab")));
    QVERIFY(!store.hasIcon(QStringLiteral("x.example")));
}

void TestTopSites::faviconClearAll()
{
    const QString dir = m_dir.path() + QStringLiteral("/favicons-clear");
    FaviconStore store(dir);
    store.capture(makeColoredIcon(0x112233), QUrl(QStringLiteral("https://a.example/")));
    store.capture(makeColoredIcon(0x445566), QUrl(QStringLiteral("https://b.example/")));
    QVERIFY(store.hasIcon(QStringLiteral("a.example")));
    QVERIFY(store.clearAll());
    QVERIFY(!store.hasIcon(QStringLiteral("a.example")));
    QVERIFY(!QFile::exists(dir + QStringLiteral("/b.example.png")));
    // The store keeps working after a clear (directory recreated).
    store.capture(makeColoredIcon(0x778899), QUrl(QStringLiteral("https://c.example/")));
    QVERIFY(store.hasIcon(QStringLiteral("c.example")));
}

QTEST_MAIN(TestTopSites)
#include "tst_topsites.moc"
