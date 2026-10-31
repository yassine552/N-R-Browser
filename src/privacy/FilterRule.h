#pragma once

#include <QString>
#include <QUrl>
#include <QRegularExpression>

// A single parsed network filter rule (Adblock-style syntax subset).
// Pure QtCore: unit-testable without WebEngine. See docs/FILTER_RULES.md
// for the documented grammar and known limitations.
class FilterRule
{
public:
    enum Type
    {
        BlockRule,
        ExceptionRule   // "@@" prefix: allow-list exception
    };

    // Content-type bit flags (mapped from WebEngine resource types by the
    // interceptor). Mirrors ABP option names.
    enum ContentType : quint32
    {
        TypeImage          = 1u << 0,
        TypeScript         = 1u << 1,
        TypeStylesheet     = 1u << 2,
        TypeSubdocument    = 1u << 3,
        TypeObject         = 1u << 4,
        TypeXmlHttpRequest = 1u << 5,
        TypePing           = 1u << 6,
        TypeDocument       = 1u << 7,
        TypeMedia          = 1u << 8,
        TypeFont           = 1u << 9,
        TypeWebSocket      = 1u << 10,
        TypeOther          = 1u << 11
    };

    Type type = BlockRule;
    quint32 contentTypes = 0;   // 0 = applies to all content types
    int thirdParty = -1;        // -1 any, 0 first-party only, 1 third-party only
    QString rawLine;
    QRegularExpression pattern;

    // Parses one rule line. Returns a rule; *ok reports whether the line
    // produced a usable network rule. *skipReason (optional) documents why
    // a line was skipped (comment, cosmetic rule, unsupported option).
    static FilterRule parse(const QString &line, bool *ok = nullptr,
                            QString *skipReason = nullptr);

    bool matches(const QUrl &requestUrl, const QUrl &firstPartyUrl,
                 quint32 resourceType) const;

    static quint32 allContentTypes();

    // "example.com" vs "cdn.example.com" -> false (first party).
    // Uses a simplified registrable-domain heuristic (last two labels);
    // multi-part public suffixes (co.uk) are a documented limitation.
    static bool isThirdParty(const QString &hostA, const QString &hostB);
    static QString registrableDomain(const QString &host);
};
