// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include "AdblockBackend.h"

#include <QReadWriteLock>

/*
 * RustAdblockBackend: ad-block engine backed by brave/adblock-rust
 * (MPL-2.0) through the adblock-sys C ABI (adblock-sys/include/nor_adblock.h).
 *
 * Capabilities beyond the built-in engine: $domain= rules (with ~negation),
 * $match-case on regex filters, full-regex filters, correct public-suffix
 * (co.uk) registrable-domain handling, cosmetic filtering resources, and
 * engine serialization for fast startup caching.
 *
 * Thread safety: the Rust engine is built Send+Sync (adblock crate without
 * the "single-thread" feature) and every C call goes through the shim;
 * a QReadWriteLock additionally guards the engine pointer itself, because
 * reload() swaps it on the GUI thread while Qt WebEngine's IO thread may
 * be deciding requests.
 *
 * Debug info: when the total offered rules stay below
 * DebugInfoRuleThreshold, per-rule debug info is enabled so diagnostics
 * can show which rule matched (FilterDecision::matchedRule, as JSON).
 * Large real-world lists skip it to keep memory low.
 */
class RustAdblockBackend : public AdblockBackend
{
public:
    static constexpr int DebugInfoRuleThreshold = 5000;

    RustAdblockBackend() = default;
    ~RustAdblockBackend() override;

    QString name() const override { return QStringLiteral("adblock-rust"); }
    bool supportsCosmeticFiltering() const override { return true; }
    bool supportsEngineCache() const override { return true; }

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

    /* Maps FilterRule content-type flags onto the request-type strings
     * understood by the engine ("script", "image", ...). Public for tests. */
    static QByteArray requestTypeForFlags(quint32 resourceType);

private:
    void destroyEngine();
    void destroyFilterSet();

    mutable QReadWriteLock m_lock;
    QStringList m_pendingLists;         // buffered until build() decides debug mode
    void *m_filterSet = nullptr;   // NorAdblockFilterSet while loading
    void *m_engine = nullptr;      // NorAdblockEngine once built
    int m_offeredBlocking = 0;
    int m_offeredExceptions = 0;
    bool m_debugInfo = false;
};
