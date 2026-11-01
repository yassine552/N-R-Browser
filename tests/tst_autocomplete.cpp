// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
// Unit tests for AddressBarSuggestions: local autocomplete from history
// and bookmarks, Arabic/French text support, disabling, de-duplication,
// and private-mode exclusion (private visits never reach the stores, so
// they can never be suggested).
#include <QtTest>

#include "AddressBarSuggestions.h"
#include "AppConfig.h"
#include "BookmarkStore.h"
#include "HistoryStore.h"

#include <QTemporaryDir>

class TestAutocomplete : public QObject
{
    Q_OBJECT

private slots:
    void hostsFromHistory();
    void bookmarksMatchTitleAndUrl();
    void historyMatchesTitle();
    void arabicAndFrenchText();
    void deduplicationAcrossSources();
    void resultLimitRespected();
    void disabledYieldsNothing();
    void emptyInputYieldsNothing();
    void privateVisitNeverSuggested();
    void urlFromDisplayParsing();

private:
    QTemporaryDir m_dir;
    AppConfig *m_config = nullptr;
    HistoryStore *m_history = nullptr;
    BookmarkStore *m_bookmarks = nullptr;

    AddressBarSuggestions *makeSuggestions();
};

AddressBarSuggestions *TestAutocomplete::makeSuggestions()
{
    return new AddressBarSuggestions(*m_config, m_history, m_bookmarks);
}

void TestAutocomplete::hostsFromHistory()
{
    m_config = new AppConfig(m_dir.filePath("a.ini"));
    m_history = new HistoryStore(QStringLiteral(":memory:"));
    m_bookmarks = new BookmarkStore(QStringLiteral(":memory:"));
    m_history->addVisit("https://example.org/docs/intro", "Intro");
    m_history->addVisit("https://example.org/docs/advanced", "Advanced");
    m_history->addVisit("https://other.net/page", "Other");

    auto *suggestions = makeSuggestions();
    // Host-level suggestion: one entry per distinct host, first-seen order.
    const QStringList forExa = suggestions->suggestionsFor(QStringLiteral("exa"));
    QVERIFY(forExa.contains("https://example.org"));

    // Typing a full host still suggests the host entry.
    const QStringList forHost = suggestions->suggestionsFor(QStringLiteral("other.net"));
    QVERIFY(forHost.contains("https://other.net"));
    delete suggestions;

    delete m_config;
    delete m_history;
    delete m_bookmarks;
}

void TestAutocomplete::bookmarksMatchTitleAndUrl()
{
    m_config = new AppConfig(m_dir.filePath("b.ini"));
    m_history = new HistoryStore(QStringLiteral(":memory:"));
    m_bookmarks = new BookmarkStore(QStringLiteral(":memory:"));
    m_bookmarks->addBookmark("Privacy news", "https://privacy.example.org/news");
    m_bookmarks->addBookmark("Documentation", "https://docs.example.net/manual");

    auto *suggestions = makeSuggestions();
    // Title match.
    QStringList hits = suggestions->suggestionsFor(QStringLiteral("privacy news"));
    QCOMPARE(hits.size(), 1);
    QVERIFY(hits.first().startsWith("https://privacy.example.org/news"));

    // URL-part match.
    hits = suggestions->suggestionsFor(QStringLiteral("manual"));
    QCOMPARE(hits.size(), 1);
    QVERIFY(hits.first().startsWith("https://docs.example.net/manual"));
    delete suggestions;

    delete m_config;
    delete m_history;
    delete m_bookmarks;
}

void TestAutocomplete::historyMatchesTitle()
{
    m_config = new AppConfig(m_dir.filePath("c.ini"));
    m_history = new HistoryStore(QStringLiteral(":memory:"));
    m_bookmarks = new BookmarkStore(QStringLiteral(":memory:"));
    m_history->addVisit("https://example.com/article/2026", "The state of privacy");

    auto *suggestions = makeSuggestions();
    const QStringList hits =
        suggestions->suggestionsFor(QStringLiteral("state of privacy"));
    QCOMPARE(hits.size(), 1);
    QVERIFY(hits.first().contains("https://example.com/article/2026"));
    QVERIFY(hits.first().contains("The state of privacy"));
    delete suggestions;

    delete m_config;
    delete m_history;
    delete m_bookmarks;
}

void TestAutocomplete::arabicAndFrenchText()
{
    m_config = new AppConfig(m_dir.filePath("d.ini"));
    m_history = new HistoryStore(QStringLiteral(":memory:"));
    m_bookmarks = new BookmarkStore(QStringLiteral(":memory:"));
    m_history->addVisit("https://arabic.example.org/مقال", "مقال عن الخصوصية");
    m_history->addVisit("https://french.example.org/journal", "Journal du numérique");

    auto *suggestions = makeSuggestions();
    // Arabic substring over the title.
    QStringList hits = suggestions->suggestionsFor(QStringLiteral("خصوصية"));
    QCOMPARE(hits.size(), 1);
    QVERIFY(hits.first().startsWith("https://arabic.example.org/"));

    // French substring with accents in the store, typed without accents.
    hits = suggestions->suggestionsFor(QStringLiteral("journal"));
    QCOMPARE(hits.size(), 1);
    QVERIFY(hits.first().startsWith("https://french.example.org/"));
    delete suggestions;

    delete m_config;
    delete m_history;
    delete m_bookmarks;
}

void TestAutocomplete::deduplicationAcrossSources()
{
    m_config = new AppConfig(m_dir.filePath("e.ini"));
    m_history = new HistoryStore(QStringLiteral(":memory:"));
    m_bookmarks = new BookmarkStore(QStringLiteral(":memory:"));
    const QString url = "https://same.example.org/page";
    m_bookmarks->addBookmark("Bookmarked page", url);
    m_history->addVisit(url, "Visited page");

    auto *suggestions = makeSuggestions();
    const QStringList hits = suggestions->suggestionsFor(QStringLiteral("same.example"));
    // The bookmark wins (checked before history) and appears exactly once.
    int occurrences = 0;
    for (const QString &hit : hits)
        if (hit.startsWith(url))
            ++occurrences;
    QCOMPARE(occurrences, 1);
    delete suggestions;

    delete m_config;
    delete m_history;
    delete m_bookmarks;
}

void TestAutocomplete::resultLimitRespected()
{
    m_config = new AppConfig(m_dir.filePath("f.ini"));
    m_history = new HistoryStore(QStringLiteral(":memory:"));
    m_bookmarks = new BookmarkStore(QStringLiteral(":memory:"));
    for (int i = 0; i < 30; ++i)
        m_history->addVisit(QStringLiteral("https://host%1.example.org/page").arg(i),
                            QStringLiteral("Page %1").arg(i));

    auto *suggestions = makeSuggestions();
    const QStringList hits = suggestions->suggestionsFor(QStringLiteral("example"));
    QVERIFY(hits.size() <= AddressBarSuggestions::DefaultResultLimit);
    QVERIFY(!hits.isEmpty());
    delete suggestions;

    delete m_config;
    delete m_history;
    delete m_bookmarks;
}

void TestAutocomplete::disabledYieldsNothing()
{
    m_config = new AppConfig(m_dir.filePath("g.ini"));
    m_history = new HistoryStore(QStringLiteral(":memory:"));
    m_bookmarks = new BookmarkStore(QStringLiteral(":memory:"));
    m_history->addVisit("https://example.org/x", "X");
    m_config->setAddressBarSuggestions(false);

    auto *suggestions = makeSuggestions();
    QVERIFY(suggestions->suggestionsFor(QStringLiteral("example")).isEmpty());
    delete suggestions;

    delete m_config;
    delete m_history;
    delete m_bookmarks;
}

void TestAutocomplete::emptyInputYieldsNothing()
{
    m_config = new AppConfig(m_dir.filePath("h.ini"));
    m_history = new HistoryStore(QStringLiteral(":memory:"));
    m_bookmarks = new BookmarkStore(QStringLiteral(":memory:"));
    m_history->addVisit("https://example.org/x", "X");

    auto *suggestions = makeSuggestions();
    QVERIFY(suggestions->suggestionsFor(QString()).isEmpty());
    QVERIFY(suggestions->suggestionsFor(QStringLiteral("   ")).isEmpty());
    delete suggestions;

    delete m_config;
    delete m_history;
    delete m_bookmarks;
}

void TestAutocomplete::privateVisitNeverSuggested()
{
    // Private windows share the SAME history store object but their pages
    // are never recorded into it (WebPage::recordHistoryIfAllowed checks
    // isOffTheRecord — covered by tst_profile / the private smoke mode).
    // Here we assert the remaining half of the contract: whatever was NOT
    // recorded can never be suggested.
    m_config = new AppConfig(m_dir.filePath("i.ini"));
    m_history = new HistoryStore(QStringLiteral(":memory:"));
    m_bookmarks = new BookmarkStore(QStringLiteral(":memory:"));

    // Simulate a private session: the page is loaded, not recorded.
    const QString privateUrl = "https://secret.example.org/private-session";
    // (no addVisit — that is exactly what a private window does)
    m_history->addVisit("https://public.example.org/page", "Public");

    auto *suggestions = makeSuggestions();
    const QStringList hits = suggestions->suggestionsFor(QStringLiteral("secret"));
    QVERIFY(hits.isEmpty());
    // The public page is suggested, plus its host entry — both correct.
    const QStringList publicHits = suggestions->suggestionsFor(QStringLiteral("public"));
    QCOMPARE(publicHits.size(), 2);
    int pageHits = 0;
    for (const QString &hit : publicHits)
        if (hit.startsWith("https://public.example.org/page"))
            ++pageHits;
    QCOMPARE(pageHits, 1);
    delete suggestions;

    delete m_config;
    delete m_history;
    delete m_bookmarks;
}

void TestAutocomplete::urlFromDisplayParsing()
{
    QCOMPARE(AddressBarSuggestions::urlFromDisplay(
                 QStringLiteral("https://example.org/page · Some Title")),
             QStringLiteral("https://example.org/page"));
    QCOMPARE(AddressBarSuggestions::urlFromDisplay(QStringLiteral("https://example.org")),
             QStringLiteral("https://example.org"));
    QCOMPARE(AddressBarSuggestions::urlFromDisplay(QString()), QString());
}

QTEST_MAIN(TestAutocomplete)
#include "tst_autocomplete.moc"
