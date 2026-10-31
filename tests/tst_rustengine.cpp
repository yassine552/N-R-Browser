// Unit tests for the RustAdblockBackend (brave/adblock-rust via the
// adblock-sys C ABI): compatibility with the built-in engine's documented
// semantics plus the extended capabilities ($domain, match-case, PSL,
// cosmetic filtering, engine serialization).
#include <QtTest>

#include "AdBlockClient.h"
#include "AppConfig.h"
#include "FilterRule.h"
#include "RustAdblockBackend.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>

class TestRustEngine : public QObject
{
    Q_OBJECT

private slots:
    // ---- basic semantics (compatibility with the built-in engine) -------
    void engineBlocksTracker();
    void exceptionRuleWhitelists();
    void disabledClientBlocksNothing();
    void clientSiteAllowlist();
    void perSiteStatistics();
    void matchedRuleDiagnostics();

    // ---- extended capabilities -------------------------------------------
    void domainOptionRules();
    void domainOptionNegation();
    void matchCaseOnRegexRules();
    void registrableDomainPsl();
    void cosmeticResources();
    void cosmeticGenerichideFlag();
    void genericHideSelectors();
    void engineSerializationRoundtrip();
    void corruptCacheRejected();
    void emptyBackendDecidesAllow();

    // ---- real-list integration (skipped when fixtures absent) -----------
    void easyListIntegration();

private:
    QString fixture(const QString &name) const;
    static long vmRssKb();

    QTemporaryDir m_dir;
};

QString TestRustEngine::fixture(const QString &name) const
{
    // tests/fixtures/filterlists relative to the source tree.
    return QFileInfo(QLatin1String(NOR_FIXTURES_DIR) + QStringLiteral("/filterlists/")
                     + name)
        .absoluteFilePath();
}

long TestRustEngine::vmRssKb()
{
    // stdio (not QFile): /proc files report size 0, which confuses Qt's
    // buffered reading.
    FILE *f = fopen("/proc/self/status", "r");
    if (!f)
        return -1;
    char line[256];
    long kb = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &kb);
            break;
        }
    }
    fclose(f);
    return kb;
}

static std::unique_ptr<RustAdblockBackend> makeBackend(const QString &listText)
{
    auto backend = std::make_unique<RustAdblockBackend>();
    backend->clear();
    backend->addList(listText);
    backend->build();
    return backend;
}

static FilterDecision decideOn(AdblockBackend *backend, const QString &url,
                               const QString &firstParty, quint32 type = 0)
{
    return backend->decide(QUrl(url), QUrl(firstParty), type);
}

// ---------------------------------------------------------------- basics ----

void TestRustEngine::engineBlocksTracker()
{
    auto backend = makeBackend(QStringLiteral("||tracker.example^\n"));
    QVERIFY(decideOn(backend.get(), "https://tracker.example/pixel",
                     "https://news.example.org/article").block);
    QVERIFY(!decideOn(backend.get(), "https://good.example/pixel",
                      "https://news.example.org/article").block);
}

void TestRustEngine::exceptionRuleWhitelists()
{
    auto backend = makeBackend(QStringLiteral("||cdn.com^\n@@||cdn.com/ok/\n"));
    QVERIFY(decideOn(backend.get(), "https://cdn.com/ad.js", "https://x.org/").block);
    QVERIFY(!decideOn(backend.get(), "https://cdn.com/ok/pic.png", "https://x.org/").block);
}

void TestRustEngine::disabledClientBlocksNothing()
{
    AppConfig config(m_dir.filePath("disabled.ini"));
    config.setAdBlockEnabled(false);
    AdBlockClient client(config);
    client.reload();
    const FilterDecision d = client.decide(QUrl("https://tracker.example/pixel"),
                                            QUrl("https://x.org/"), 0);
    QVERIFY(!d.block);
}

void TestRustEngine::clientSiteAllowlist()
{
    AppConfig config(m_dir.filePath("allow.ini"));
    config.setAdBlockAllowlist({ QStringLiteral("friendly.example") });
    QTemporaryFile list;
    list.open();
    list.write("||ads.example^\n");
    list.close();
    config.setCustomFilterLists({ list.fileName() });
    AdBlockClient client(config);
    client.reload();
    QVERIFY(!client.decide(QUrl("https://ads.friendly.example/x"),
                           QUrl("https://friendly.example/"), 0).block);
    QVERIFY(client.decide(QUrl("https://ads.example/x"),
                          QUrl("https://other.example/"), 0).block);
}

void TestRustEngine::perSiteStatistics()
{
    AppConfig config(m_dir.filePath("stats.ini"));
    AdBlockClient client(config);
    client.reload();
    QCOMPARE(client.blockedTotal(), 0);
    client.noteBlockedForSite(QStringLiteral("news.example"));
    client.noteBlockedForSite(QStringLiteral("news.example"));
    client.noteBlockedForSite(QStringLiteral("shop.example"));
    QCOMPARE(client.blockedTotal(), 3);
    QCOMPARE(client.blockedForSite(QStringLiteral("news.example")), 2);
    QCOMPARE(client.blockedForSite(QStringLiteral("shop.example")), 1);
    QCOMPARE(client.blockedForSite(QStringLiteral("other.example")), 0);
    client.resetBlockedTotal();
    QCOMPARE(client.blockedForSite(QStringLiteral("news.example")), 0);
}

void TestRustEngine::matchedRuleDiagnostics()
{
    // Below the debug threshold: the engine records rule info as JSON.
    auto backend = makeBackend(QStringLiteral("||tracker.example^\n"));
    const FilterDecision d = decideOn(backend.get(), "https://tracker.example/pixel",
                                      "https://x.org/");
    QVERIFY(d.block);
    QVERIFY2(d.matchedRule.contains("tracker.example"),
             qPrintable(d.matchedRule));
}

// ------------------------------------------------------------ capabilities --

void TestRustEngine::domainOptionRules()
{
    auto backend = makeBackend(QStringLiteral("banner$domain=foo.com|bar.com\n"));
    QVERIFY(decideOn(backend.get(), "https://cdn.example.org/banner",
                     "https://foo.com/").block);
    QVERIFY(decideOn(backend.get(), "https://cdn.example.org/banner",
                     "https://sub.bar.com/").block);
    QVERIFY(!decideOn(backend.get(), "https://cdn.example.org/banner",
                      "https://other.com/").block);
}

void TestRustEngine::domainOptionNegation()
{
    auto backend = makeBackend(QStringLiteral("banner$domain=foo.com|~bar.foo.com\n"));
    QVERIFY(decideOn(backend.get(), "https://cdn.example.org/banner",
                     "https://foo.com/").block);
    QVERIFY(!decideOn(backend.get(), "https://cdn.example.org/banner",
                      "https://bar.foo.com/").block);
}

void TestRustEngine::matchCaseOnRegexRules()
{
    // $match-case is only valid with full-regex filters (ABP semantics);
    // the built-in engine skips such rules entirely, the Rust engine
    // honors them.
    auto backend = makeBackend(QStringLiteral("/BannerAd/$match-case\n"));
    QVERIFY(decideOn(backend.get(), "https://x.com/img/BannerAd.png",
                     "https://x.com/", FilterRule::TypeImage).block);
    QVERIFY(!decideOn(backend.get(), "https://x.com/img/bannerad.png",
                      "https://x.com/", FilterRule::TypeImage).block);
}

void TestRustEngine::registrableDomainPsl()
{
    // co.uk is a two-part public suffix: the built-in engine's simplified
    // heuristic mis-classifies these; the Rust engine embeds the PSL.
    auto backend = makeBackend(QStringLiteral("||ads.example.co.uk^\n"));
    QVERIFY(decideOn(backend.get(), "https://tracker.ads.example.co.uk/x.js",
                     "https://site.com/").block);
    QVERIFY(!decideOn(backend.get(), "https://ads.example.co.uk.evil.com/x.js",
                      "https://site.com/").block);
    // first-party across a co.uk registrable domain
    auto backend2 = makeBackend(QStringLiteral("||analytics.co.uk^$third-party\n"));
    QVERIFY(!decideOn(backend2.get(), "https://analytics.co.uk/x.js",
                      "https://www.analytics.co.uk/").block);
    QVERIFY(decideOn(backend2.get(), "https://analytics.co.uk/x.js",
                     "https://publisher.com/").block);
}

void TestRustEngine::cosmeticResources()
{
    auto backend = makeBackend(QStringLiteral(
        "example.com##.ad-banner\n"
        "example.com##.sponsored-link\n"
        "example.com#@#.whitelisted-box\n"));
    const CosmeticResources res =
        backend->cosmeticResources(QUrl("https://example.com/page"));
    QVERIFY(res.isValid);
    QVERIFY(res.hideSelectors.contains(QStringLiteral(".ad-banner")));
    QVERIFY(res.hideSelectors.contains(QStringLiteral(".sponsored-link")));
    QVERIFY(res.exceptions.contains(QStringLiteral(".whitelisted-box")));
    // Other domains do not get example.com-specific rules.
    const CosmeticResources other =
        backend->cosmeticResources(QUrl("https://other.org/"));
    QVERIFY(!other.hideSelectors.contains(QStringLiteral(".ad-banner")));
}

void TestRustEngine::cosmeticGenerichideFlag()
{
    // $generichide is an exception-rule option: "on this site, disable
    // generic cosmetic hiding" (the engine rejects it on block rules).
    auto backend = makeBackend(QStringLiteral(
        "@@||example.com^$generichide\n"
        "##.generic-banner\n"));
    const CosmeticResources res =
        backend->cosmeticResources(QUrl("https://example.com/page"));
    QVERIFY(res.isValid);
    QVERIFY(res.genericHide);   // page opts out of generic hiding
    const CosmeticResources other =
        backend->cosmeticResources(QUrl("https://other.org/"));
    QVERIFY(!other.genericHide);
}

void TestRustEngine::genericHideSelectors()
{
    auto backend = makeBackend(QStringLiteral(
        "##.generic-hidden\n"
        "##.generic-ad\n"
        "##.never-on-page\n"));
    const QStringList selectors = backend->genericHideSelectors(
        { QStringLiteral("generic-hidden"), QStringLiteral("generic-ad") },
        { QStringLiteral("page-header") },
        {});
    QVERIFY(selectors.contains(QStringLiteral(".generic-hidden")));
    QVERIFY(selectors.contains(QStringLiteral(".generic-ad")));
    QVERIFY(!selectors.contains(QStringLiteral(".never-on-page")));
    // Exceptions remove selectors.
    const QStringList filtered = backend->genericHideSelectors(
        { QStringLiteral("generic-hidden"), QStringLiteral("generic-ad") },
        {}, { QStringLiteral(".generic-ad") });
    QVERIFY(filtered.contains(QStringLiteral(".generic-hidden")));
    QVERIFY(!filtered.contains(QStringLiteral(".generic-ad")));
}

void TestRustEngine::engineSerializationRoundtrip()
{
    auto backend = makeBackend(QStringLiteral(
        "||tracker.example^\n"
        "example.com##.ad-banner\n"));
    const QByteArray data = backend->serializeEngine();
    QVERIFY(!data.isEmpty());

    RustAdblockBackend restored;
    restored.clear();
    QVERIFY(restored.deserializeEngine(data));
    QVERIFY(decideOn(&restored, "https://tracker.example/pixel",
                     "https://x.org/").block);
    QVERIFY(!decideOn(&restored, "https://good.example/", "https://x.org/").block);
    const CosmeticResources res =
        restored.cosmeticResources(QUrl("https://example.com/"));
    QVERIFY(res.isValid);
    QVERIFY(res.hideSelectors.contains(QStringLiteral(".ad-banner")));
}

void TestRustEngine::corruptCacheRejected()
{
    RustAdblockBackend backend;
    backend.clear();
    QVERIFY(!backend.deserializeEngine(QByteArrayLiteral("garbage-not-an-engine")));
    QVERIFY(!backend.deserializeEngine(QByteArray()));
    // A truncated valid cache must also be rejected.
    auto good = makeBackend(QStringLiteral("||tracker.example^\n"));
    const QByteArray data = good->serializeEngine();
    QVERIFY(!backend.deserializeEngine(data.left(data.size() / 2)));
}

void TestRustEngine::emptyBackendDecidesAllow()
{
    RustAdblockBackend backend;
    backend.clear();
    backend.build();   // no lists at all
    QVERIFY(!decideOn(&backend, "https://tracker.example/x", "https://y.org/").block);
    QVERIFY(backend.cosmeticResources(QUrl("https://x.org/")).hideSelectors.isEmpty());
}

// ---------------------------------------------------------- real EasyList ---

void TestRustEngine::easyListIntegration()
{
    const QString easylist = fixture(QStringLiteral("easylist.txt"));
    const QString easyprivacy = fixture(QStringLiteral("easyprivacy.txt"));
    if (!QFile::exists(easylist) || !QFile::exists(easyprivacy)) {
        QSKIP("offline fixtures (easylist.txt/easyprivacy.txt) not available");
    }

    const long rssBefore = vmRssKb();
    QElapsedTimer timer;
    timer.start();

    RustAdblockBackend backend;
    backend.clear();
    QFile f1(easylist);
    QVERIFY(f1.open(QIODevice::ReadOnly | QIODevice::Text));
    backend.addList(QString::fromUtf8(f1.readAll()));
    QFile f2(easyprivacy);
    QVERIFY(f2.open(QIODevice::ReadOnly | QIODevice::Text));
    backend.addList(QString::fromUtf8(f2.readAll()));
    backend.build();
    const qint64 buildMs = timer.elapsed();
    const long rssAfter = vmRssKb();

    // Well-known tracker URLs blocked by the real lists.
    QVERIFY(decideOn(&backend, "https://securepubads.g.doubleclick.net/tag/js/gpt.js",
                     "https://www.example.com/", FilterRule::TypeScript).block);
    QVERIFY(decideOn(&backend, "https://www.google-analytics.com/analytics.js",
                     "https://shop.example.net/", FilterRule::TypeScript).block);
    QVERIFY(decideOn(&backend, "https://pagead2.googlesyndication.com/pagead/adshow",
                     "https://news.example.org/", FilterRule::TypeImage).block);
    // Ordinary first-party content stays allowed.
    QVERIFY(!decideOn(&backend, "https://www.example.com/js/app.js",
                      "https://www.example.com/", FilterRule::TypeScript).block);

    // Performance sample on the full engine.
    timer.restart();
    int hits = 0;
    for (int i = 0; i < 5000; ++i) {
        hits += decideOn(&backend, "https://pagead2.googlesyndication.com/pagead/adshow?i=" + QString::number(i),
                         "https://news.example.org/", FilterRule::TypeImage).block ? 1 : 0;
    }
    const qint64 perRequestUs = timer.nsecsElapsed() / 1000 / 5000;
    QCOMPARE(hits, 5000);
    QVERIFY2(perRequestUs < 200, qPrintable(QString("too slow: %1 us/request").arg(perRequestUs)));

    qInfo("easylist+easyprivacy: build %lld ms, %d offered rules, RSS delta %ld kB, %lld us/request",
          buildMs, backend.blockRuleCount(), rssAfter - rssBefore, perRequestUs);

    // Serialization cache timing: deserializing must be dramatically
    // faster than re-parsing (this is the startup-cache rationale).
    const QByteArray cache = backend.serializeEngine();
    QVERIFY(!cache.isEmpty());
    timer.restart();
    RustAdblockBackend restored;
    restored.clear();
    QVERIFY(restored.deserializeEngine(cache));
    const qint64 restoreMs = timer.elapsed();
    qInfo("engine cache: %d bytes, deserialize %lld ms (vs %lld ms parse)",
          cache.size(), restoreMs, buildMs);
    QVERIFY2(restoreMs < buildMs, "deserialization should beat re-parsing");
    QVERIFY(decideOn(&restored, "https://www.google-analytics.com/analytics.js",
                     "https://shop.example.net/", FilterRule::TypeScript).block);
}

QTEST_MAIN(TestRustEngine)
#include "tst_rustengine.moc"
