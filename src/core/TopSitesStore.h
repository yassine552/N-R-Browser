#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include "HistoryStore.h"

// Firefox-style "Top Sites": the sites the user actually visits most,
// derived from the LOCAL browsing history only — no external service is
// ever contacted, nothing leaves the machine.
//
// Ranking (frecency approximation):
//   The history schema keeps one row per URL (lifetime visitCount +
//   lastVisit), so score = visitCount * 0.5^(ageDays / 14): one half-life
//   is 14 days. A site visited heavily long ago decays below a site
//   visited a few times this week — frequency AND recency both matter.
//   Pinned sites always keep their slot regardless of score.
//
// Grouping: entries are grouped by BASE DOMAIN — "www.youtube.com",
// "youtube.com" and "m.youtube.com" are ONE tile (two-level public suffixes
// like "co.uk" are handled, "www." is stripped). The best-scoring URL of
// the base domain represents it (ties prefer the shorter path, so "host/"
// beats a deep article link), and the tile carries every host seen under
// the base domain as a favicon fallback chain.
//
// User state (pins, removals) lives in a small JSON file next to the
// history DB:
//   - pin: survives ranking AND history clears (explicit user action,
//     like a bookmark);
//   - remove ("Dismiss"): derived entries are hidden by host, pinned
//     entries by URL. The removal list is itself history-derived data, so
//     clearing the history wipes it.
//
// Privacy contract:
//   - private/off-the-record pages never enter HistoryStore (enforced by
//     WebPage::recordHistoryIfAllowed), so they can never become Top
//     Sites and can never mutate this list;
//   - no additional per-visit history is stored to compute the ranking.
class TopSitesStore : public QObject
{
    Q_OBJECT
public:
    // history may be nullptr (feature degrades to pins-only).
    explicit TopSitesStore(HistoryStore *history, const QString &statePath,
                           QObject *parent = nullptr);

    // Pinned entries first (in pin order), then frecency-ranked derived
    // entries, at most `limit` total.
    QVector<TopSite> topSites(int limit) const;

    bool pinSite(const QUrl &url, const QString &title);
    bool unpinSite(const QUrl &url);
    bool isPinned(const QUrl &url) const;
    // Hides a tile: by URL when it is a pin, by host when it is derived
    // (so other pages of the same site do not sneak back in).
    bool removeSite(const QUrl &url, const QString &title);
    // Editing a derived tile converts it into a pin with the new values
    // (Firefox behaviour) and hides the old URL.
    bool editSite(const QUrl &oldUrl, const QUrl &newUrl, const QString &newTitle);
    // Wipe pins + removals (used when the user clears everything).
    bool clearAllState();

    // Frecency: visitCount decays with a 14-day half-life. Pure —
    // unit-tested. Age is clamped at zero (future timestamps are treated
    // as "now", e.g. clock skew after the fact).
    static qreal frecencyScore(int visitCount, qint64 lastVisit, qint64 nowSecs);
    // Lower-cased host of an http(s) URL, empty otherwise.
    static QString hostOf(const QString &url);
    // Base domain of a host: "www.youtube.com" -> "youtube.com",
    // "news.bbc.co.uk" -> "bbc.co.uk". Pure — unit-tested.
    static QString baseDomainOf(const QString &host);

signals:
    void topSitesChanged();

private:
    void load();
    bool save();
    void scheduleRefresh();

    struct Pin
    {
        QString url;
        QString title;
    };

    HistoryStore *m_history = nullptr;
    QString m_statePath;
    QVector<Pin> m_pinned;
    QStringList m_removedHosts;
    QStringList m_removedUrls;
    QTimer m_refreshDebounce;
};
