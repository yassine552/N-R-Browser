#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class AppConfig;
class BookmarkStore;
class HistoryStore;

struct AddressSuggestion
{
    QString url;
    QString display;   // "url · title" (or "url" for bare hosts)
};

/*
 * AddressBarSuggestions: local-only autocomplete source for the address
 * bar. Combines (in this order):
 *   1. previously visited hosts (prefix/contains match on the host),
 *   2. bookmark matches (title and URL),
 *   3. history matches (title and URL), newest first.
 *
 * Guarantees:
 *  - purely local: history, bookmarks and typed text never leave the
 *    device (no remote suggestion service exists in NØR at all);
 *  - private-window activity can never appear here, because private
 *    pages are never written to the history store;
 *  - results are de-duplicated by URL and capped (default 8);
 *  - can be disabled by the user (AppConfig::addressBarSuggestions).
 *
 * Pure logic on top of the stores: unit-testable without widgets.
 */
class AddressBarSuggestions : public QObject
{
    Q_OBJECT
public:
    static constexpr int DefaultResultLimit = 8;

    explicit AddressBarSuggestions(AppConfig &config, HistoryStore *history,
                                   BookmarkStore *bookmarks,
                                   QObject *parent = nullptr);

    // Visible for tests: the raw ranking pipeline.
    QVector<AddressSuggestion> rankedSuggestions(const QString &text,
                                                 int limit = DefaultResultLimit) const;

    // Entry list for a QCompleter model: display strings where the URL
    // is the first " · "-separated segment (AddressBar extracts it on
    // activation). Empty when suggestions are disabled.
    QStringList suggestionsFor(const QString &text) const;

    static QString urlFromDisplay(const QString &display);

private:
    bool enabled() const;

    AppConfig &m_config;
    HistoryStore *m_history;
    BookmarkStore *m_bookmarks;
};
