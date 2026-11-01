// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

struct FilterDecision
{
    bool block = false;
    QString matchedRule;   // human-readable rule text/JSON for diagnostics
};

/*
 * Cosmetic-filter resources for one page URL (CSS selector hiding).
 * `injectedScript` is exposed by the engine but intentionally unused:
 * NØR applies CSS-only cosmetic filtering and does not inject
 * scriptlets from filter lists (documented security policy).
 */
struct CosmeticResources
{
    QStringList hideSelectors;   // URL-specific rules that apply on this page
    QStringList exceptions;      // selectors re-allowed by exception rules
    QString injectedScript;      // engine scriptlets (unused, see above)
    bool genericHide = false;    // $generichide: page opts out of generic hiding
    bool isValid = false;
};

/*
 * AdblockBackend: swappable ad-block engine behind AdBlockClient.
 *
 * Implementations:
 *  - BuiltInAdblockBackend: the NØR C++ FilterRule engine (subset of the
 *    Adblock syntax, no cosmetic filtering).
 *  - RustAdblockBackend: wrapper over brave/adblock-rust via the
 *    adblock-sys C ABI (full EasyList/EasyPrivacy support incl. $domain,
 *    match-case, cosmetic filtering, engine serialization).
 *
 * Thread contract: decide(), cosmeticResources() and genericHideSelectors()
 * may be called from Qt WebEngine's IO thread concurrently with GUI-thread
 * calls of the loading functions. Implementations must guard shared state.
 *
 * Loading protocol (called by AdBlockClient::reload()):
 *   clear() → addList(text)* → build()
 */
class AdblockBackend
{
public:
    virtual ~AdblockBackend() = default;

    virtual QString name() const = 0;
    virtual bool supportsCosmeticFiltering() const = 0;
    virtual bool supportsEngineCache() const = 0;

    virtual void clear() = 0;
    virtual void addList(const QString &listText) = 0;
    virtual void build() = 0;

    /* Statistics reported in the settings dialog. "Offered" counts are
     * line-based approximations for engines that do not expose exact
     * numbers; skipped() counts lines the engine could not use. */
    virtual int blockRuleCount() const = 0;
    virtual int exceptionRuleCount() const = 0;
    virtual int skippedRuleCount() const = 0;

    virtual FilterDecision decide(const QUrl &requestUrl, const QUrl &firstPartyUrl,
                                  quint32 resourceType) const = 0;

    /* Cosmetic capabilities (return invalid/empty results when the
     * engine does not support them). */
    virtual CosmeticResources cosmeticResources(const QUrl &pageUrl) const = 0;
    virtual QStringList genericHideSelectors(const QStringList &classes,
                                             const QStringList &ids,
                                             const QStringList &exceptions) const = 0;

    /* Engine cache: serialize() the built engine for fast startup;
     * deserialize() replaces the engine without re-parsing lists. */
    virtual QByteArray serializeEngine() const = 0;
    virtual bool deserializeEngine(const QByteArray &data) = 0;
};
