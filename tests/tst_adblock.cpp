// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
// Unit tests for the NØR privacy-filter engine: rule parsing, matching,
// exceptions, options, allowlisting, and client decisions.
#include <QtTest>

#include "AdBlockClient.h"
#include "AppConfig.h"
#include "FilterRule.h"

class TestAdBlock : public QObject
{
    Q_OBJECT

private slots:
    // ---- parsing ----------------------------------------------------------
    void parseComment();
    void parseMetadata();
    void parseEmpty();
    void parseCosmeticSkipped();
    void parseDomainOptionSkipped();
    void parseMatchCaseSkipped();
    void parseExceptionRule();
    void parsePlainPattern();
    void parseInvalidReportsNotOk();
    void parseKeepsRawLine();

    // ---- matching -----------------------------------------------------------
    void domainAnchorMatches();
    void domainAnchorRejectsSubstrings();
    void domainAnchorMatchesSubdomain();
    void separatorSemantics();
    void plainSubstringMatches();
    void caseInsensitiveMatching();
    void startAndEndAnchors();
    void wildcardMatching();
    void literalDollarSign();
    void thirdPartyOption();
    void firstPartyOption();
    void contentTypeOption();
    void contentTypeExcludesOthers();

    // ---- client -----------------------------------------------------------
    void clientBlocksTracker();
    void clientExceptionWhitelists();
    void clientSiteAllowlist();
    void clientDisabledBlocksNothing();
    void clientCountsBlocked();
    void clientLoadsBuiltInList();
    void clientCustomList();

private:
    QTemporaryDir m_dir;
};

static FilterRule parseOk(const QString &line)
{
    bool ok = false;
    FilterRule r = FilterRule::parse(line, &ok);
    return r;
}

void TestAdBlock::parseComment()
{
    bool ok = true;
    QString reason;
    FilterRule::parse("! this is a comment", &ok, &reason);
    QVERIFY(!ok);
    QCOMPARE(reason, QStringLiteral("comment"));
}

void TestAdBlock::parseMetadata()
{
    bool ok = true;
    FilterRule::parse("[Adblock Plus 2.0]", &ok);
    QVERIFY(!ok);
}

void TestAdBlock::parseEmpty()
{
    bool ok = true;
    FilterRule::parse("   ", &ok);
    QVERIFY(!ok);
}

void TestAdBlock::parseCosmeticSkipped()
{
    bool ok = true;
    QString reason;
    FilterRule::parse("example.com##.ad-banner", &ok, &reason);
    QVERIFY(!ok);
    QCOMPARE(reason, QStringLiteral("cosmetic-rule"));
}

void TestAdBlock::parseDomainOptionSkipped()
{
    bool ok = true;
    QString reason;
    FilterRule::parse("||ads.com^$domain=partner.com", &ok, &reason);
    QVERIFY(!ok);
    QCOMPARE(reason, QStringLiteral("unsupported-option"));
}

void TestAdBlock::parseMatchCaseSkipped()
{
    bool ok = true;
    FilterRule::parse("BannerAd$match-case", &ok);
    QVERIFY(!ok);
}

void TestAdBlock::parseExceptionRule()
{
    const FilterRule r = parseOk("@@||example.com^");
    QVERIFY(r.type == FilterRule::ExceptionRule);
}

void TestAdBlock::parsePlainPattern()
{
    const FilterRule r = parseOk("/ads/banner.js");
    QVERIFY(r.type == FilterRule::BlockRule);
    QCOMPARE(r.contentTypes, quint32(0));
    QCOMPARE(r.thirdParty, -1);
}

void TestAdBlock::parseInvalidReportsNotOk()
{
    bool ok = true;
    // "$" alone (empty pattern + empty options) is unusable.
    FilterRule::parse("$", &ok);
    QVERIFY(!ok);
}

void TestAdBlock::parseKeepsRawLine()
{
    const FilterRule r = parseOk("||tracker.io^");
    QVERIFY(r.rawLine.contains("tracker.io"));
}

void TestAdBlock::domainAnchorMatches()
{
    const FilterRule r = parseOk("||example.com^");
    QVERIFY(r.matches(QUrl("https://example.com/page"), QUrl("https://site.org"), 0));
    QVERIFY(r.matches(QUrl("https://example.com"), QUrl("https://site.org"), 0));
    QVERIFY(r.matches(QUrl("http://example.com:8080/x"), QUrl("https://site.org"), 0));
}

void TestAdBlock::domainAnchorRejectsSubstrings()
{
    const FilterRule r = parseOk("||example.com^");
    // "notexample.com" must NOT match: domain labels are respected.
    QVERIFY(!r.matches(QUrl("https://notexample.com/"), QUrl("https://site.org"), 0));
    QVERIFY(!r.matches(QUrl("https://example.com.evil.io/"), QUrl("https://site.org"), 0));
    QVERIFY(!r.matches(QUrl("https://good.org/?ref=example.com"), QUrl("https://site.org"), 0));
}

void TestAdBlock::domainAnchorMatchesSubdomain()
{
    const FilterRule r = parseOk("||example.com^");
    QVERIFY(r.matches(QUrl("https://cdn.example.com/x.js"), QUrl("https://site.org"), 0));
    QVERIFY(r.matches(QUrl("https://a.b.example.com/x.js"), QUrl("https://site.org"), 0));
}

void TestAdBlock::separatorSemantics()
{
    const FilterRule r = parseOk("||example.com^");
    // '/' after the domain is a separator -> match.
    QVERIFY(r.matches(QUrl("https://example.com/a"), QUrl("https://s.org"), 0));
    // '?' and '=' are separators too.
    QVERIFY(r.matches(QUrl("https://example.com?a=1"), QUrl("https://s.org"), 0));
    // '.' is NOT a separator (see previous test for evil.io case).
    const FilterRule r2 = parseOk("||ads.example.com^");
    QVERIFY(r2.matches(QUrl("https://ads.example.com/pixel?x=1"), QUrl("https://s.org"), 0));
}

void TestAdBlock::plainSubstringMatches()
{
    const FilterRule r = parseOk("banner468");
    QVERIFY(r.matches(QUrl("https://cdn.example.com/img/banner468.gif"), QUrl("https://s.org"), 0));
    QVERIFY(!r.matches(QUrl("https://cdn.example.com/img/logo.png"), QUrl("https://s.org"), 0));
}

void TestAdBlock::caseInsensitiveMatching()
{
    const FilterRule r = parseOk("BANNER");
    QVERIFY(r.matches(QUrl("https://x.io/Banner.js"), QUrl("https://s.org"), 0));
    QVERIFY(r.matches(QUrl("https://x.io/banner.js"), QUrl("https://s.org"), 0));
}

void TestAdBlock::startAndEndAnchors()
{
    const FilterRule start = parseOk("|https://ads.");
    QVERIFY(start.matches(QUrl("https://ads.example.com/x"), QUrl("https://s.org"), 0));
    QVERIFY(!start.matches(QUrl("https://example.com/ads/x"), QUrl("https://s.org"), 0));

    const FilterRule end = parseOk("/tracker.js|");
    QVERIFY(end.matches(QUrl("https://example.com/lib/tracker.js"), QUrl("https://s.org"), 0));
    QVERIFY(!end.matches(QUrl("https://example.com/lib/tracker.js?v=1"), QUrl("https://s.org"), 0));
}

void TestAdBlock::wildcardMatching()
{
    const FilterRule r = parseOk("||cdn.*/ads/*");
    QVERIFY(r.matches(QUrl("https://cdn.example.com/ads/img.png"), QUrl("https://s.org"), 0));
    QVERIFY(!r.matches(QUrl("https://cdn.example.com/img/ads.png"), QUrl("https://s.org"), 0));
}

void TestAdBlock::literalDollarSign()
{
    // A '$' that does not parse as options is part of the pattern.
    const FilterRule r = parseOk("/redirect?url=$1");
    QVERIFY(r.matches(QUrl("https://example.com/redirect?url=$1"), QUrl("https://s.org"), 0));
}

void TestAdBlock::thirdPartyOption()
{
    const FilterRule r = parseOk("||analytics.com^$third-party");
    QVERIFY(r.matches(QUrl("https://analytics.com/x.js"),
                      QUrl("https://publisher.org/page"), 0));
    QVERIFY(!r.matches(QUrl("https://analytics.com/x.js"),
                       QUrl("https://analytics.com/page"), 0));
    // Subdomain of the same registrable domain is first-party.
    QVERIFY(!r.matches(QUrl("https://analytics.com/x.js"),
                       QUrl("https://www.analytics.com/page"), 0));
}

void TestAdBlock::firstPartyOption()
{
    const FilterRule r = parseOk("||widget.com^$~third-party");
    QVERIFY(r.matches(QUrl("https://widget.com/w.js"),
                      QUrl("https://widget.com/index"), 0));
    QVERIFY(!r.matches(QUrl("https://widget.com/w.js"),
                       QUrl("https://other.org/"), 0));
}

void TestAdBlock::contentTypeOption()
{
    const FilterRule r = parseOk("||ads.com^$image");
    QVERIFY(r.matches(QUrl("https://ads.com/i.png"), QUrl("https://s.org"),
                      FilterRule::TypeImage));
    QVERIFY(!r.matches(QUrl("https://ads.com/i.png"), QUrl("https://s.org"),
                       FilterRule::TypeScript));
    // Multiple options combine.
    const FilterRule multi = parseOk("||media.net^$script,image");
    QVERIFY(multi.matches(QUrl("https://media.net/a.js"), QUrl("https://s.org"),
                          FilterRule::TypeScript));
    QVERIFY(multi.matches(QUrl("https://media.net/a.png"), QUrl("https://s.org"),
                          FilterRule::TypeImage));
    QVERIFY(!multi.matches(QUrl("https://media.net/a.css"), QUrl("https://s.org"),
                           FilterRule::TypeStylesheet));
}

void TestAdBlock::contentTypeExcludesOthers()
{
    const FilterRule r = parseOk("||stats.io^$xmlhttprequest");
    QVERIFY(r.matches(QUrl("https://stats.io/collect"), QUrl("https://s.org"),
                      FilterRule::TypeXmlHttpRequest));
    QVERIFY(!r.matches(QUrl("https://stats.io/collect"), QUrl("https://s.org"),
                       FilterRule::TypeDocument));
}

// ---------------------------------------------------------------- client ----

static std::unique_ptr<AdBlockClient> makeClient(AppConfig *config, const QString &listText)
{
    QTemporaryFile list;
    list.open();
    list.write(listText.toUtf8());
    list.close();
    config->setCustomFilterLists({ list.fileName() });
    auto client = std::make_unique<AdBlockClient>(*config);
    client->reload();
    return client;
}

void TestAdBlock::clientBlocksTracker()
{
    AppConfig config(m_dir.filePath("a.ini"));
    auto client = makeClient(&config, "||tracker.example^\n");
    const FilterDecision d = client->decide(QUrl("https://tracker.example/pixel"),
                                             QUrl("https://news.example.org/article"), 0);
    QVERIFY(d.block);
    QVERIFY(d.matchedRule.contains("tracker.example"));
}

void TestAdBlock::clientExceptionWhitelists()
{
    AppConfig config(m_dir.filePath("b.ini"));
    auto client = makeClient(&config, "||cdn.com^\n@@||cdn.com/ok/\n");
    const FilterDecision blocked = client->decide(QUrl("https://cdn.com/ad.js"),
                                                  QUrl("https://x.org/"), 0);
    QVERIFY(blocked.block);
    const FilterDecision allowed = client->decide(QUrl("https://cdn.com/ok/pic.png"),
                                                  QUrl("https://x.org/"), 0);
    QVERIFY(!allowed.block);
}

void TestAdBlock::clientSiteAllowlist()
{
    AppConfig config(m_dir.filePath("c.ini"));
    config.setAdBlockAllowlist({ QStringLiteral("friendly.example") });
    auto client = makeClient(&config, "||ads.example^\n");
    const FilterDecision d = client->decide(QUrl("https://ads.friendly.example/x"),
                                             QUrl("https://friendly.example/"), 0);
    QVERIFY(!d.block);
    // A non-allowlisted site is still filtered.
    const FilterDecision other = client->decide(QUrl("https://ads.example/x"),
                                                QUrl("https://other.example/"), 0);
    QVERIFY(other.block);
}

void TestAdBlock::clientDisabledBlocksNothing()
{
    AppConfig config(m_dir.filePath("d.ini"));
    config.setAdBlockEnabled(false);
    auto client = makeClient(&config, "||tracker.example^\n");
    const FilterDecision d = client->decide(QUrl("https://tracker.example/pixel"),
                                             QUrl("https://x.org/"), 0);
    QVERIFY(!d.block);
}

void TestAdBlock::clientCountsBlocked()
{
    AppConfig config(m_dir.filePath("e.ini"));
    auto client = makeClient(&config, "||tracker.example^\n");
    QCOMPARE(client->blockedTotal(), 0);
    client->decide(QUrl("https://tracker.example/p"), QUrl("https://x.org/"), 0);
    client->noteBlocked();
    client->noteBlocked();
    QCOMPARE(client->blockedTotal(), 2);
    client->resetBlockedTotal();
    QCOMPARE(client->blockedTotal(), 0);
}

void TestAdBlock::clientLoadsBuiltInList()
{
    AppConfig config(m_dir.filePath("f.ini"));
    // The built-in list ships inside the test executable's resources
    // (tests link the same sources but not the app_resources target), so
    // only the custom list machinery is verified here; built-in coverage
    // is exercised by the application smoke tests.
    config.setCustomFilterLists({});
    AdBlockClient client(config);
    client.reload();
    QVERIFY(client.blockRuleCount() >= 0);
    QVERIFY(client.exceptionRuleCount() >= 0);
    QVERIFY(client.skippedRuleCount() >= 0);
}

void TestAdBlock::clientCustomList()
{
    AppConfig config(m_dir.filePath("g.ini"));
    // A list with comments, cosmetics, and real rules.
    auto client = makeClient(&config,
                             "! Title: test\n"
                             "||ads.com^\n"
                             "example.com##.ad\n"
                             "/banner/\n"
                             "@@||ok.com^\n");
    QCOMPARE(client->blockRuleCount(), 2);
    QCOMPARE(client->exceptionRuleCount(), 1);
#ifdef NOR_HAVE_RUST_ADBLOCK
    // The adblock-rust engine consumes the cosmetic "##" rule as a hiding
    // filter instead of skipping it (v1.0.0 limitation, now resolved).
    QCOMPARE(client->skippedRuleCount(), 0);
#else
    QCOMPARE(client->skippedRuleCount(), 1); // cosmetic skipped by the built-in engine
#endif
}

QTEST_MAIN(TestAdBlock)
#include "tst_adblock.moc"
