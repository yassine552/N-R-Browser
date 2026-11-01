// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct HistoryEntry
{
    qint64 id = 0;
    QString url;
    QString title;
    qint64 lastVisit = 0;   // epoch seconds
    int visitCount = 0;
};

// Firefox-style "Top Sites" entry: the tile the new-tab page renders.
// url is the page the tile opens; host is the real host used for favicon
// lookup; baseDomain is the grouping key (ONE tile per site: www.x.com,
// x.com and m.x.com collapse together); faviconHosts lists the other hosts
// seen under the same base domain, best first, so the icon can fall back
// when the representative host has no cached favicon; pinned entries
// survive ranking and history clears (like bookmarks); score is the frecency
// value used to order derived entries.
struct TopSite
{
    QString url;
    QString title;
    QString host;
    QString baseDomain;
    QStringList faviconHosts;
    bool pinned = false;
    qreal score = 0.0;
};

// SQLite-backed browsing history. Private-window pages never reach this
// store (enforced by the caller layer, WebPage::recordHistoryIfAllowed()).
// All statements are prepared; no string concatenation builds queries.
class HistoryStore : public QObject
{
    Q_OBJECT
public:
    // path may be ":memory:" for tests.
    explicit HistoryStore(const QString &dbPath, QObject *parent = nullptr);
    ~HistoryStore() override;

    // Upserts by URL: bumps visit count and last-visit time, refreshes title.
    bool addVisit(const QString &url, const QString &title);
    // Same, with an explicit visit time. Seeding/testing hook — the browser
    // itself always records "now". Lets tests build realistic age profiles
    // for the Top-Sites frecency ranking.
    bool addVisit(const QString &url, const QString &title, qint64 atEpochSecs);
    // Refreshes the title of the most recent entry for url (pages often
    // settle their title after loadFinished).
    bool updateTitleForUrl(const QString &url, const QString &title);
    QVector<HistoryEntry> recent(int limit = 100) const;
    QVector<HistoryEntry> mostVisited(int limit = 8) const;
    // Every row (capped) — input for the Top-Sites host ranking.
    QVector<HistoryEntry> allEntries(int cap = 4000) const;
    // Substring search over title and URL, newest first.
    QVector<HistoryEntry> search(const QString &needle, int limit = 200) const;
    bool removeEntry(qint64 id);
    bool removeByUrl(const QString &url);
    bool clearAll();
    qint64 count() const;

    bool isValid() const { return m_valid; }
    QString lastError() const { return m_lastError; }

signals:
    void historyChanged();
    // Emitted ONLY by clearAll(): derived data (Top-Sites removal lists,
    // favicon caches) must wipe themselves because they are history data,
    // while explicit pins survive like bookmarks.
    void historyCleared();

private:
    bool open(const QString &dbPath);
    bool exec(const QString &sql);
    static QString escapeLike(const QString &s);

    bool m_valid = false;
    QString m_lastError;
    QString m_connectionName;
};
