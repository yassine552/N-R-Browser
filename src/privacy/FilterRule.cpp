#include "FilterRule.h"

#include <QHash>

using namespace std::chrono_literals;

namespace {

struct OptionToken
{
    const char *name;
    quint32 contentType; // 0 = non-content option
    int thirdParty;      // -1 = unrelated
    bool unsupported;    // recognized but not implementable -> skip rule
};

const OptionToken kOptions[] = {
    { "image",          FilterRule::TypeImage,          -1, false },
    { "script",         FilterRule::TypeScript,         -1, false },
    { "stylesheet",     FilterRule::TypeStylesheet,     -1, false },
    { "subdocument",    FilterRule::TypeSubdocument,    -1, false },
    { "object",         FilterRule::TypeObject,         -1, false },
    { "object-subrequest", FilterRule::TypeObject,      -1, false },
    { "xmlhttprequest", FilterRule::TypeXmlHttpRequest, -1, false },
    { "xhr",            FilterRule::TypeXmlHttpRequest, -1, false },
    { "ping",           FilterRule::TypePing,           -1, false },
    { "document",       FilterRule::TypeDocument,       -1, false },
    { "main-frame",     FilterRule::TypeDocument,       -1, false },
    { "media",          FilterRule::TypeMedia,          -1, false },
    { "font",           FilterRule::TypeFont,           -1, false },
    { "websocket",      FilterRule::TypeWebSocket,      -1, false },
    { "other",          FilterRule::TypeOther,          -1, false },
    { "third-party",    0,                               1, false },
    { "1p",             0,                               0, false },
    { "first-party",    0,                               0, false },
    { "3p",             0,                               1, false },
    { "~third-party",   0,                               0, false },
    { "collapse",       0,                              -1, false }, // cosmetic hint, no-op
    { "important",      0,                              -1, false }, // precedence: no-op (documented)
    { "match-case",     0,                              -1, true  }, // case-sensitive: unsupported
    { "domain",         0,                              -1, true  }, // per-domain rules: unsupported
    { "csp",            0,                              -1, true  },
    { "rewrite",        0,                              -1, true  },
    { "removeparam",    0,                              -1, true  },
    { "webrtc",         0,                              -1, true  },
    { "popup",          0,                              -1, true  },
};

bool lookupOption(const QString &token, const OptionToken **out)
{
    for (const OptionToken &o : kOptions) {
        if (token == QLatin1String(o.name)) {
            *out = &o;
            return true;
        }
    }
    return false;
}

// Translates the ABP pattern part into a regular expression pattern string.
// The rule and the URL are matched lower-case (case-insensitive by design).
QString buildRegex(const QString &body)
{
    QString rx;
    rx.reserve(body.size() * 2);

    bool domainAnchor = false;
    QString work = body;
    if (work.startsWith(QLatin1String("||"))) {
        domainAnchor = true;
        work.remove(0, 2);
    } else if (work.startsWith(QLatin1Char('|'))) {
        rx += QLatin1Char('^');
        work.remove(0, 1);
    }

    const bool endAnchor = work.endsWith(QLatin1Char('|'));
    if (endAnchor)
        work.chop(1);

    if (domainAnchor)
        rx = QStringLiteral("^") + QStringLiteral("(?:(?:https?|wss?):\\/\\/)?(?:[a-z0-9\\-]+\\.)*");

    static const QString separator = QStringLiteral("([^a-z0-9_.%-]|$)");
    QString escaped;
    escaped.reserve(work.size() * 2);
    for (const QChar c : work) {
        if (c == QLatin1Char('^'))
            escaped += separator;
        else if (c == QLatin1Char('*'))
            escaped += QLatin1String(".*");
        else
            escaped += QRegularExpression::escape(QString(c));
    }
    rx += escaped;

    if (endAnchor)
        rx += QLatin1Char('$');
    return rx;
}
} // namespace

quint32 FilterRule::allContentTypes()
{
    return TypeImage | TypeScript | TypeStylesheet | TypeSubdocument | TypeObject
           | TypeXmlHttpRequest | TypePing | TypeDocument | TypeMedia | TypeFont
           | TypeWebSocket | TypeOther;
}

FilterRule FilterRule::parse(const QString &line, bool *ok, QString *skipReason)
{
    if (ok)
        *ok = false;
    FilterRule rule;
    QString work = line.trimmed();
    if (skipReason)
        skipReason->clear();

    if (work.isEmpty()) {
        if (skipReason)
            *skipReason = QStringLiteral("empty");
        return rule;
    }
    if (work.startsWith(QLatin1Char('!'))) {
        if (skipReason)
            *skipReason = QStringLiteral("comment");
        return rule;
    }
    if (work.startsWith(QLatin1Char('['))) {
        if (skipReason)
            *skipReason = QStringLiteral("metadata");
        return rule;
    }
    // Cosmetic rules (element hiding) cannot be enforced by a network
    // interceptor.
    if (work.contains(QLatin1String("##")) || work.contains(QLatin1String("#@#"))) {
        if (skipReason)
            *skipReason = QStringLiteral("cosmetic-rule");
        return rule;
    }

    rule.type = work.startsWith(QLatin1String("@@")) ? ExceptionRule : BlockRule;
    if (rule.type == ExceptionRule)
        work.remove(0, 2);

    // Options: everything after the last '$' if it parses as option tokens.
    const int dollar = work.lastIndexOf(QLatin1Char('$'));
    if (dollar >= 0) {
        const QString optionsPart = work.mid(dollar + 1);
        const QStringList tokens = optionsPart.split(QLatin1Char(','),
                                                     Qt::SkipEmptyParts);
        bool allKnown = !tokens.isEmpty();
        bool unsupported = false;
        for (const QString &rawTok : tokens) {
            const OptionToken *opt = nullptr;
            if (!lookupOption(rawTok, &opt)) {
                // Option-with-value forms ("domain=...", "csp=...", ...) are
                // recognized as options we cannot honor -> skip the rule
                // rather than treat '$' as a literal and risk false matches.
                static const QStringList kValueOptions = {
                    QStringLiteral("domain"), QStringLiteral("csp"),
                    QStringLiteral("rewrite"), QStringLiteral("removeparam"),
                };
                const QString base = rawTok.section(QLatin1Char('='), 0, 0);
                if (kValueOptions.contains(base)) {
                    unsupported = true;
                    allKnown = true;
                    continue;
                }
                allKnown = false;
                break;
            }
            if (opt->unsupported)
                unsupported = true;
        }
        if (allKnown) {
            if (unsupported) {
                if (skipReason)
                    *skipReason = QStringLiteral("unsupported-option");
                return rule;
            }
            for (const QString &rawTok : tokens) {
                const OptionToken *opt = nullptr;
                lookupOption(rawTok, &opt);
                if (opt->contentType)
                    rule.contentTypes |= opt->contentType;
                else if (opt->thirdParty >= 0)
                    rule.thirdParty = opt->thirdParty;
            }
            work.truncate(dollar);
        }
        // else: '$' is a literal character in the pattern.
    }

    if (work.isEmpty()) {
        if (skipReason)
            *skipReason = QStringLiteral("empty-pattern");
        return rule;
    }
    if (work == QLatin1String("$")) {
        // A lone '$' is not a meaningful rule.
        if (skipReason)
            *skipReason = QStringLiteral("empty-pattern");
        return rule;
    }

    rule.rawLine = line;
    const QString rx = buildRegex(work.toLower());
    rule.pattern.setPattern(rx);
    if (!rule.pattern.isValid()) {
        if (skipReason)
            *skipReason = QStringLiteral("bad-regex");
        return rule;
    }
    rule.pattern.optimize();
    if (ok)
        *ok = true;
    return rule;
}

bool FilterRule::matches(const QUrl &requestUrl, const QUrl &firstPartyUrl,
                         quint32 resourceType) const
{
    if (contentTypes && (resourceType & contentTypes) == 0)
        return false;

    if (thirdParty >= 0) {
        const bool third = isThirdParty(requestUrl.host().toLower(),
                                        firstPartyUrl.host().toLower());
        if ((thirdParty == 1) != third)
            return false;
    }

    const QString urlStr = requestUrl.toString(QUrl::FullyEncoded).toLower();
    return pattern.match(urlStr).hasMatch();
}

QString FilterRule::registrableDomain(const QString &host)
{
    if (host.isEmpty())
        return QString();
    const QStringList labels = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (labels.size() <= 2)
        return host;
    // Simplified: last two labels. Known limitation for co.uk-style
    // public suffixes (documented).
    return labels.at(labels.size() - 2) + QLatin1Char('.') + labels.last();
}

bool FilterRule::isThirdParty(const QString &hostA, const QString &hostB)
{
    if (hostA.isEmpty() || hostB.isEmpty())
        return false;
    if (hostA == hostB)
        return false;
    return registrableDomain(hostA) != registrableDomain(hostB);
}
