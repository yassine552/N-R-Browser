// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "AddressBarSuggestions.h"

#include "AppConfig.h"
#include "BookmarkStore.h"
#include "HistoryStore.h"

#include <QSet>
#include <QUrl>
#include <QVector>

namespace {
// Display separator between URL and title. A "·" keeps the URL cleanly
// extractable and reads well in LTR and RTL contexts.
// (QStringLiteral, not QLatin1String: the source file is UTF-8 and the
// middle dot is a multi-byte character.)
const QString kSeparator = QStringLiteral(" · ");
} // namespace

AddressBarSuggestions::AddressBarSuggestions(AppConfig &config,
                                             HistoryStore *history,
                                             BookmarkStore *bookmarks,
                                             QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_history(history)
    , m_bookmarks(bookmarks)
{
}

bool AddressBarSuggestions::enabled() const
{
    return m_config.addressBarSuggestions();
}

QString AddressBarSuggestions::urlFromDisplay(const QString &display)
{
    const int cut = display.indexOf(kSeparator);
    if (cut <= 0)
        return display;
    return display.left(cut).trimmed();
}

QVector<AddressSuggestion> AddressBarSuggestions::rankedSuggestions(const QString &text,
                                                                     int limit) const
{
    QVector<AddressSuggestion> out;
    if (limit <= 0)
        return out;

    const QString needle = text.trimmed();
    if (needle.isEmpty() || !enabled())
        return out;

    // Contains-matching, case-insensitive: works for Arabic, French and
    // English alike (diacritic-insensitive matching is a documented
    // limitation).
    const QString needleLower = needle.toLower();
    auto contains = [&needleLower](const QString &haystack) {
        return haystack.toLower().contains(needleLower);
    };

    QSet<QString> seenUrls;

    // 1) Previously visited hosts (matches "exa" against "example.org").
    if (m_history && m_history->isValid()) {
        const QVector<HistoryEntry> recent = m_history->recent(500);
        for (const HistoryEntry &entry : recent) {
            const QUrl url(entry.url);
            const QString host = url.host();
            if (host.isEmpty() || seenUrls.contains(host))
                continue;
            if (contains(host)) {
                AddressSuggestion s;
                s.url = url.scheme() + QStringLiteral("://") + host;
                s.display = s.url;
                out.append(s);
                seenUrls.insert(host);
                if (out.size() >= limit)
                    return out;
            }
        }
    }

    // 2) Bookmarks (title and URL matching).
    if (m_bookmarks && m_bookmarks->isValid()) {
        const QVector<Bookmark> all = m_bookmarks->bookmarks();
        for (const Bookmark &bookmark : all) {
            if (seenUrls.contains(bookmark.url))
                continue;
            if (!contains(bookmark.url) && !contains(bookmark.title))
                continue;
            AddressSuggestion s;
            s.url = bookmark.url;
            s.display = bookmark.title.isEmpty()
                             ? bookmark.url
                             : bookmark.url + kSeparator + bookmark.title;
            out.append(s);
            seenUrls.insert(bookmark.url);
            if (out.size() >= limit)
                return out;
        }
    }

    // 3) History (title and URL matching), newest first.
    if (m_history && m_history->isValid()) {
        const QVector<HistoryEntry> hits = m_history->search(needle, 200);
        for (const HistoryEntry &entry : hits) {
            if (seenUrls.contains(entry.url))
                continue;
            // The SQL search already matched title/URL; keep the order.
            AddressSuggestion s;
            s.url = entry.url;
            s.display = entry.title.isEmpty()
                             ? entry.url
                             : entry.url + kSeparator + entry.title;
            out.append(s);
            seenUrls.insert(entry.url);
            if (out.size() >= limit)
                return out;
        }
    }

    return out;
}

QStringList AddressBarSuggestions::suggestionsFor(const QString &text) const
{
    QStringList display;
    for (const AddressSuggestion &s : rankedSuggestions(text))
        display.append(s.display);
    return display;
}
