// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace NorCore {

/* Resource type of a network request. Mirrors the subset of
 * QWebEngineUrlRequestInfo::ResourceType that this engine reasons about.
 * Kept decoupled from QtWebEngine so the logic is testable headlessly. */
enum class ResourceType {
    Other,
    MainFrame,
    SubFrame,
    Stylesheet,
    Script,
    Image,
    Font,
    Media,
    WebSocket,
    Xhr,
    Popup,
};

enum class FilterDecision {
    Allowed,
    Blocked,
};

/*
 * PrivacyFilter: a local, EasyList-compatible (subset) request filter engine.
 *
 * Supported syntax:
 *   - "!" comments, blank lines, "[Adblock Plus 2.0]" headers  (ignored)
 *   - element-hiding rules "##...", "#@#...", "#$#..."          (ignored)
 *   - domain anchor   ||example.com^
 *   - start anchor    |https://...
 *   - end anchor      banner.gif|
 *   - wildcards       ads.*.com
 *   - separator       ^   (matches any non-alphanumeric separator or EOL)
 *   - exceptions      @@||example.com^
 *   - options         $third-party, $~third-party, $domain=a|b (with ~negation),
 *                     $image/$script/$stylesheet/$media/$font/$xmlhttprequest/
 *                     $subdocument/$document/$websocket/$popup/$other
 *                     (and their ~negations), $match-case.
 *                     $important/$elemhide/$generichide/$genericblock are
 *                     accepted but intentionally no-ops in this engine.
 *
 * Semantics: a request is blocked when any blocking rule matches; a matching
 * exception rule always wins over blocking rules.
 */
class PrivacyFilter : public QObject
{
    Q_OBJECT

public:
    explicit PrivacyFilter(QObject *parent = nullptr);

    bool isEnabled() const;
    void setEnabled(bool enabled);

    int ruleCount() const;
    int listCount() const;
    QStringList listNames() const;

    /* Loads (or replaces) a named filter list. Returns the number of rules
     * that were actually added. Element-hiding and invalid lines are skipped. */
    int loadList(const QString &listName, const QString &rules);
    void removeList(const QString &listName);
    void clearLists();

    /* First-party hosts for which filtering is fully disabled. */
    void setSiteWhitelist(const QStringList &hosts);
    QStringList siteWhitelist() const;

    FilterDecision decide(const QUrl &requestUrl,
                          const QUrl &firstPartyUrl,
                          ResourceType type = ResourceType::Other) const;

    /* "www.a.example.com" -> "example.com"; handles common two-part
     * registry suffixes such as "co.uk" or "com.au" (simplified list). */
    static QString baseDomain(const QString &host);

    /* host equals pattern or is a subdomain of it. */
    static bool hostMatches(const QString &host, const QString &pattern);

    static bool isThirdPartyUrl(const QUrl &requestUrl, const QUrl &firstPartyUrl);

signals:
    void rulesChanged(int ruleCount);
    void enabledChanged(bool enabled);

private:
    struct Rule {
        QRegularExpression pattern;
        bool isException = false;
        bool caseSensitive = false;
        int thirdParty = 0;             // 0 = any, 1 = third-party only, -1 = first-party only
        QStringList domainFilters;      // $domain= restrictions on the first-party host
        QVector<ResourceType> types;    // positive type options
        QVector<ResourceType> typesNeg; // negated type options ($~image, ...)
        QString listName;               // owning list (enables removeList())
        QString source;                 // original rule text, kept for debugging
    };

    static bool parseRule(const QString &line, const QString &listName, Rule *outRule);
    static QRegularExpression compilePattern(QString pattern, bool caseSensitive);
    bool ruleMatches(const Rule &rule, const QUrl &requestUrl,
                     const QUrl &firstPartyUrl, ResourceType type) const;

    bool m_enabled = true;
    QStringList m_listNames;
    QStringList m_siteWhitelist;
    QVector<Rule> m_rules;
};

} // namespace NorCore
