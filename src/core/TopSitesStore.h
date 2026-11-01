// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
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
//   Derived entries must clear a small score floor: a site nobody touched
//   for months does not fill a slot just because the history is short.
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
// Search-engine exclusion: result pages of the user's search engines
// ("x at DuckDuckGo", "x - Google Search", ...) are a by-product of
// FINDING sites, not sites the user chose to visit. They must never take
// a derived slot (Firefox and Brave both filter them). The match is on
// the engine's URL PATTERN — same base domain AND carrying the engine's
// query key ("?q=", "?search=") — so a content site that doubles as an
// engine (Wikipedia) only loses its /w/index.php?search= pages, never
// its articles. Pins are exempt — a pinned engine page is an explicit
// user action.
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

    // Engine result pages never produce DERIVED tiles. Input: the engines'
    // URL templates ("https://duckduckgo.com/?q=%s"). Empty by default;
    // set from the configured engines at startup and whenever the engine
    // list changes.
    void setEngineUrlTemplates(const QStringList &templates);
    QStringList engineUrlTemplates() const { return m_engineTemplates; }
    // True when `url` is a result page of one of `templates`' engines
    // (same base domain + the engine's query key). Pure — unit-tested.
    static bool isEnginePageUrl(const QString &url, const QStringList &templates);
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
    QStringList m_engineTemplates;
    QTimer m_refreshDebounce;
};
