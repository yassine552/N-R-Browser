// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "BuiltInAdblockBackend.h"

#include "FilterRule.h"

#include <QReadWriteLock>

BuiltInAdblockBackend::~BuiltInAdblockBackend() = default;

void BuiltInAdblockBackend::clear()
{
    QWriteLocker locker(&m_lock);
    m_blockRules.clear();
    m_exceptionRules.clear();
    m_skipped = 0;
    m_offeredBlocking = 0;
    m_offeredExceptions = 0;
}

void BuiltInAdblockBackend::addList(const QString &listText)
{
    // Parsing happens immediately; build() is a no-op for this backend.
    QWriteLocker locker(&m_lock);
    const QStringList lines = listText.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        QString reason;
        bool ok = false;
        const FilterRule rule = FilterRule::parse(rawLine, &ok, &reason);
        if (!ok) {
            if (reason != QLatin1String("comment") && reason != QLatin1String("empty")
                && reason != QLatin1String("metadata")) {
                ++m_skipped;
            }
            continue;
        }
        if (rule.type == FilterRule::ExceptionRule) {
            m_exceptionRules.append(rule);
            ++m_offeredExceptions;
        } else {
            m_blockRules.append(rule);
            ++m_offeredBlocking;
        }
    }
}

void BuiltInAdblockBackend::build()
{
    // Rules were parsed eagerly in addList(); nothing to finalize.
}

int BuiltInAdblockBackend::blockRuleCount() const
{
    QReadLocker locker(&m_lock);
    return m_blockRules.size();
}

int BuiltInAdblockBackend::exceptionRuleCount() const
{
    QReadLocker locker(&m_lock);
    return m_exceptionRules.size();
}

int BuiltInAdblockBackend::skippedRuleCount() const
{
    QReadLocker locker(&m_lock);
    return m_skipped;
}

FilterDecision BuiltInAdblockBackend::decide(const QUrl &requestUrl,
                                             const QUrl &firstPartyUrl,
                                             quint32 resourceType) const
{
    QReadLocker locker(&m_lock);
    FilterDecision d;
    // Exception ("@@") rules override blocking rules.
    for (const FilterRule &r : m_exceptionRules) {
        if (r.matches(requestUrl, firstPartyUrl, resourceType))
            return d;
    }
    // Blocking rules.
    for (const FilterRule &r : m_blockRules) {
        if (r.matches(requestUrl, firstPartyUrl, resourceType)) {
            d.block = true;
            d.matchedRule = r.rawLine.trimmed();
            return d;
        }
    }
    return d;
}

CosmeticResources BuiltInAdblockBackend::cosmeticResources(const QUrl &) const
{
    return CosmeticResources();   // not supported by the built-in engine
}

QStringList BuiltInAdblockBackend::genericHideSelectors(const QStringList &,
                                                        const QStringList &,
                                                        const QStringList &) const
{
    return {};
}

QByteArray BuiltInAdblockBackend::serializeEngine() const
{
    return {};
}

bool BuiltInAdblockBackend::deserializeEngine(const QByteArray &)
{
    return false;
}
