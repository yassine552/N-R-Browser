// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include "AdblockBackend.h"
#include "FilterRule.h"

#include <QReadWriteLock>
#include <QVector>

/*
 * BuiltInAdblockBackend: the NØR C++ FilterRule engine, extracted from
 * AdBlockClient v1.0.0 so the client can delegate to swappable backends.
 * Kept as the zero-dependency fallback build path and as the reference
 * implementation for the documented Adblock-syntax subset.
 *
 * Thread safety: a QReadWriteLock guards the rule set so the WebEngine
 * IO thread can decide() while the GUI thread reloads.
 */
class BuiltInAdblockBackend : public AdblockBackend
{
public:
    BuiltInAdblockBackend() = default;
    ~BuiltInAdblockBackend() override;

    QString name() const override { return QStringLiteral("built-in"); }
    bool supportsCosmeticFiltering() const override { return false; }
    bool supportsEngineCache() const override { return false; }

    void clear() override;
    void addList(const QString &listText) override;
    void build() override;

    int blockRuleCount() const override;
    int exceptionRuleCount() const override;
    int skippedRuleCount() const override;

    FilterDecision decide(const QUrl &requestUrl, const QUrl &firstPartyUrl,
                          quint32 resourceType) const override;

    CosmeticResources cosmeticResources(const QUrl &pageUrl) const override;
    QStringList genericHideSelectors(const QStringList &classes,
                                     const QStringList &ids,
                                     const QStringList &exceptions) const override;

    QByteArray serializeEngine() const override;
    bool deserializeEngine(const QByteArray &data) override;

private:
    QVector<FilterRule> m_blockRules;
    QVector<FilterRule> m_exceptionRules;
    int m_skipped = 0;
    int m_offeredBlocking = 0;
    int m_offeredExceptions = 0;
    mutable QReadWriteLock m_lock;
};
