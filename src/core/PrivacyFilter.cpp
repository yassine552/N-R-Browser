#include "PrivacyFilter.h"

#include <QHash>
#include <QSet>

namespace NorCore {

namespace {

bool isIgnoredLine(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty())
        return true;
    if (t.startsWith(QLatin1Char('!')))
        return true;                                  // comment
    if (t.startsWith(QLatin1Char('[')))
        return true;                                  // list metadata header
    if (t.startsWith(QLatin1String("##"))
        || t.startsWith(QLatin1String("#@#"))
        || t.startsWith(QLatin1String("#$#"))
        || t.startsWith(QLatin1String("#@#$")))
        return true;                                  // element hiding: out of scope
    return false;
}

const QSet<QString> &knownOptionTokens()
{
    static const QSet<QString> tokens = {
        QStringLiteral("third-party"),
        QStringLiteral("image"), QStringLiteral("script"),
        QStringLiteral("stylesheet"), QStringLiteral("media"),
        QStringLiteral("font"), QStringLiteral("xmlhttprequest"),
        QStringLiteral("subdocument"), QStringLiteral("document"),
        QStringLiteral("websocket"), QStringLiteral("popup"),
        QStringLiteral("other"), QStringLiteral("match-case"),
        QStringLiteral("important"), QStringLiteral("elemhide"),
        QStringLiteral("generichide"), QStringLiteral("genericblock"),
    };
    return tokens;
}

const QHash<QString, ResourceType> &typeOptionMap()
{
    static const QHash<QString, ResourceType> map = {
        { QStringLiteral("image"), ResourceType::Image },
        { QStringLiteral("script"), ResourceType::Script },
        { QStringLiteral("stylesheet"), ResourceType::Stylesheet },
        { QStringLiteral("media"), ResourceType::Media },
        { QStringLiteral("font"), ResourceType::Font },
        { QStringLiteral("xmlhttprequest"), ResourceType::Xhr },
        { QStringLiteral("subdocument"), ResourceType::SubFrame },
        { QStringLiteral("document"), ResourceType::MainFrame },
        { QStringLiteral("websocket"), ResourceType::WebSocket },
        { QStringLiteral("popup"), ResourceType::Popup },
        { QStringLiteral("other"), ResourceType::Other },
    };
    return map;
}

/* Heuristic: everything after the last '$' is treated as an option list only
 * when every comma-separated token is a known option (or domain=...). This
 * avoids mangling URLs that legitimately contain '$'. */
bool looksLikeOptionList(const QString &candidate)
{
    if (candidate.isEmpty() || candidate.contains(QLatin1String("://")))
        return false;
    const QStringList tokens = candidate.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return false;
    for (const QString &raw : tokens) {
        QString token = raw.trimmed();
        if (token.startsWith(QLatin1Char('~')))
            token.remove(0, 1);
        if (token.startsWith(QLatin1String("domain=")))
            continue;
        if (!knownOptionTokens().contains(token))
            return false;
    }
    return true;
}

} // namespace

PrivacyFilter::PrivacyFilter(QObject *parent)
    : QObject(parent)
{
}

bool PrivacyFilter::isEnabled() const
{
    return m_enabled;
}

void PrivacyFilter::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged(enabled);
}

int PrivacyFilter::ruleCount() const
{
    return m_rules.count();
}

int PrivacyFilter::listCount() const
{
    return m_listNames.count();
}

QStringList PrivacyFilter::listNames() const
{
    return m_listNames;
}

int PrivacyFilter::loadList(const QString &listName, const QString &rules)
{
    removeList(listName);   // reload semantics: replace an existing list

    int added = 0;
    const QStringList lines = rules.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (isIgnoredLine(line))
            continue;
        Rule rule;
        if (parseRule(line.trimmed(), listName, &rule)) {
            m_rules.append(rule);
            ++added;
        }
    }
    m_listNames.append(listName);
    emit rulesChanged(m_rules.count());
    return added;
}

void PrivacyFilter::removeList(const QString &listName)
{
    if (!m_listNames.removeOne(listName))
        return;
    for (int i = m_rules.count() - 1; i >= 0; --i) {
        if (m_rules.at(i).listName == listName)
            m_rules.removeAt(i);
    }
    emit rulesChanged(m_rules.count());
}

void PrivacyFilter::clearLists()
{
    m_rules.clear();
    m_listNames.clear();
    emit rulesChanged(0);
}

void PrivacyFilter::setSiteWhitelist(const QStringList &hosts)
{
    m_siteWhitelist.clear();
    for (const QString &host : hosts) {
        const QString h = host.trimmed().toLower();
        if (!h.isEmpty())
            m_siteWhitelist.append(h);
    }
}

QStringList PrivacyFilter::siteWhitelist() const
{
    return m_siteWhitelist;
}

FilterDecision PrivacyFilter::decide(const QUrl &requestUrl, const QUrl &firstPartyUrl,
                                     ResourceType type) const
{
    if (!m_enabled)
        return FilterDecision::Allowed;
    if (!requestUrl.isValid() || requestUrl.host().isEmpty())
        return FilterDecision::Allowed;

    // Per-site whitelist: filtering is fully disabled while browsing this site.
    const QString firstPartyHost = firstPartyUrl.host().toLower();
    if (!m_siteWhitelist.isEmpty() && !firstPartyHost.isEmpty()) {
        for (const QString &allowed : m_siteWhitelist) {
            if (hostMatches(firstPartyHost, allowed))
                return FilterDecision::Allowed;
        }
    }

    bool blocked = false;
    for (const Rule &rule : m_rules) {
        if (!ruleMatches(rule, requestUrl, firstPartyUrl, type))
            continue;
        if (rule.isException)
            return FilterDecision::Allowed;   // exception wins over blocking
        blocked = true;
    }
    return blocked ? FilterDecision::Blocked : FilterDecision::Allowed;
}

bool PrivacyFilter::parseRule(const QString &line, const QString &listName, Rule *outRule)
{
    if (line.isEmpty() || line.contains(QLatin1String("##"))
        || line.contains(QLatin1String("#@#")))
        return false;                                  // element hiding with domain prefix

    QString text = line;
    if (text.startsWith(QLatin1String("@@"))) {
        outRule->isException = true;
        text.remove(0, 2);
    }

    const int dollar = text.lastIndexOf(QLatin1Char('$'));
    if (dollar > 0) {
        const QString candidate = text.mid(dollar + 1);
        if (looksLikeOptionList(candidate)) {
            text = text.left(dollar);
            const QStringList tokens = candidate.split(QLatin1Char(','));
            for (const QString &raw : tokens) {
                QString token = raw.trimmed();
                bool negated = false;
                if (token.startsWith(QLatin1Char('~'))) {
                    negated = true;
                    token.remove(0, 1);
                }
                if (token == QLatin1String("third-party")) {
                    outRule->thirdParty = negated ? -1 : 1;
                } else if (token == QLatin1String("match-case")) {
                    outRule->caseSensitive = true;
                } else if (token.startsWith(QLatin1String("domain="))) {
                    const QStringList domains =
                        token.mid(QStringLiteral("domain=").size())
                              .split(QLatin1Char('|'), Qt::SkipEmptyParts);
                    for (const QString &d : domains)
                        outRule->domainFilters.append(d.trimmed().toLower());
                } else if (typeOptionMap().contains(token)) {
                    const ResourceType t = typeOptionMap().value(token);
                    if (negated)
                        outRule->typesNeg.append(t);
                    else
                        outRule->types.append(t);
                }
                // "important", "elemhide", "generichide", "genericblock":
                // accepted for compatibility, intentionally no effect here.
            }
        }
    }

    if (text.isEmpty() || text == QLatin1String("/"))
        return false;                                  // nothing left to match

    outRule->pattern = compilePattern(text, outRule->caseSensitive);
    if (!outRule->pattern.isValid())
        return false;
    outRule->listName = listName;
    outRule->source = line;
    return true;
}

QRegularExpression PrivacyFilter::compilePattern(QString pattern, bool caseSensitive)
{
    QString re;

    if (pattern.startsWith(QLatin1String("||"))) {
        // Domain anchor: scheme://<optional subdomains.>rest-of-rule
        re += QLatin1String("^[a-z][a-z0-9+.-]*://(?:[a-z0-9-_.]*\\.)?");
        pattern.remove(0, 2);
    } else if (pattern.startsWith(QLatin1Char('|'))) {
        re += QLatin1Char('^');
        pattern.remove(0, 1);
    }

    bool endAnchor = false;
    if (pattern.endsWith(QLatin1Char('|'))) {
        endAnchor = true;
        pattern.chop(1);
    }

    static const QString regexSpecial = QStringLiteral(".+*?^$()[]{}|\\");
    for (const QChar &c : pattern) {
        if (c == QLatin1Char('*'))
            re += QLatin1String(".*");
        else if (c == QLatin1Char('^'))
            re += QLatin1String("(?:[^a-zA-Z0-9._%-]|$)");   // separator or end of URL
        else if (regexSpecial.contains(c)) {
            re += QLatin1Char('\\');
            re += c;
        } else {
            re += c;
        }
    }
    if (endAnchor)
        re += QLatin1Char('$');

    const QRegularExpression::PatternOptions options = caseSensitive
        ? QRegularExpression::NoPatternOption
        : QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(re, options);
}

bool PrivacyFilter::ruleMatches(const Rule &rule, const QUrl &requestUrl,
                                const QUrl &firstPartyUrl, ResourceType type) const
{
    const QRegularExpressionMatch match = rule.pattern.match(requestUrl.toString());
    if (!match.hasMatch())
        return false;

    if (rule.thirdParty != 0) {
        const bool thirdParty = isThirdPartyUrl(requestUrl, firstPartyUrl);
        if (rule.thirdParty == 1 && !thirdParty)
            return false;
        if (rule.thirdParty == -1 && thirdParty)
            return false;
    }

    if (!rule.domainFilters.isEmpty()) {
        const QString firstPartyHost = firstPartyUrl.host().toLower();
        bool negativeHit = false;
        bool positiveHit = false;
        bool hasPositive = false;
        for (const QString &domain : rule.domainFilters) {
            if (domain.startsWith(QLatin1Char('~'))) {
                if (hostMatches(firstPartyHost, domain.mid(1)))
                    negativeHit = true;
            } else {
                hasPositive = true;
                if (hostMatches(firstPartyHost, domain))
                    positiveHit = true;
            }
        }
        if (negativeHit)
            return false;
        if (hasPositive && !positiveHit)
            return false;
    }

    const bool hasTypeOptions = !rule.types.isEmpty() || !rule.typesNeg.isEmpty();
    if (hasTypeOptions) {
        if (!rule.types.isEmpty() && !rule.types.contains(type))
            return false;
        if (rule.typesNeg.contains(type))
            return false;
    }
    return true;
}

QString PrivacyFilter::baseDomain(const QString &host)
{
    QString h = host.toLower();
    while (h.endsWith(QLatin1Char('.')))
        h.chop(1);
    if (h.isEmpty())
        return QString();

    const QStringList parts = h.split(QLatin1Char('.'));
    if (parts.count() <= 2)
        return h;

    // Simplified public-suffix handling for common two-part registry suffixes.
    static const QSet<QString> registrySuffixes = {
        QStringLiteral("co"), QStringLiteral("com"), QStringLiteral("org"),
        QStringLiteral("net"), QStringLiteral("gov"), QStringLiteral("edu"),
        QStringLiteral("ac"), QStringLiteral("or"), QStringLiteral("ne"),
        QStringLiteral("govt"),
    };
    const QString secondLevel = parts.at(parts.count() - 2);
    if (registrySuffixes.contains(secondLevel))
        return parts.mid(parts.count() - 3).join(QLatin1Char('.'));
    return parts.mid(parts.count() - 2).join(QLatin1Char('.'));
}

bool PrivacyFilter::hostMatches(const QString &host, const QString &pattern)
{
    const QString h = host.toLower();
    const QString p = pattern.toLower();
    if (p.isEmpty())
        return false;
    if (h == p)
        return true;
    return h.endsWith(QLatin1Char('.') + p);
}

bool PrivacyFilter::isThirdPartyUrl(const QUrl &requestUrl, const QUrl &firstPartyUrl)
{
    const QString a = baseDomain(requestUrl.host());
    const QString b = baseDomain(firstPartyUrl.host());
    if (a.isEmpty() || b.isEmpty())
        return true;   // unknown first party -> treat as third party
    return a != b;
}

} // namespace NorCore
