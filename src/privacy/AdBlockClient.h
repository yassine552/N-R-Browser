// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include "AdblockBackend.h"

class AppConfig;

// Owns the ad-block backend (built-in engine or adblock-rust), the site
// allowlist and the blocking statistics; answers "should this request be
// blocked?" for the request interceptor. Pure QtCore besides QObject
// wiring: directly unit-testable.
//
// Threading: decide() is called on Qt WebEngine's IO thread while reload()
// and the cosmetic helpers run on the GUI thread. The backend guards its
// own state; the statistics below use a dedicated mutex.
class AdBlockClient : public QObject
{
    Q_OBJECT
public:
    explicit AdBlockClient(AppConfig &config, QObject *parent = nullptr);
    ~AdBlockClient() override;

    // (Re)loads built-in list + custom lists + allowlist from settings.
    void reload();

    // GUI thread: re-reads the ad-block toggle + site allowlist from
    // settings into a thread-safe cache. decide() runs on WebEngine's IO
    // thread and must not touch QSettings there. reload() calls this too.
    void refreshConfigCache();

    FilterDecision decide(const QUrl &requestUrl, const QUrl &firstPartyUrl,
                          quint32 resourceType) const;

    // True when the host itself is allow-listed (site exception).
    bool hostAllowed(const QString &host) const;

    int blockRuleCount() const;
    int exceptionRuleCount() const;
    int skippedRuleCount() const;
    int blockedTotal() const;
    // Blocked requests attributed to one site (first-party host).
    int blockedForSite(const QString &firstPartyHost) const;
    QStringList loadWarnings() const;

    // Backend identity (shown in About/diagnostics).
    QString backendName() const;
    bool cosmeticFilteringSupported() const;
    bool engineCacheSupported() const;

    // Cosmetic filtering (no-ops on engines without support).
    CosmeticResources cosmeticResources(const QUrl &pageUrl) const;
    QStringList genericHideSelectors(const QStringList &classes,
                                     const QStringList &ids,
                                     const QStringList &exceptions) const;

    // Engine cache (no-ops on engines without support).
    QByteArray serializeEngine() const;
    bool deserializeEngine(const QByteArray &data);

    void resetBlockedTotal();
    // Called by the interceptor whenever a request is actually blocked.
    void noteBlocked();
    // Counts the block against the site (first-party host) as well.
    void noteBlockedForSite(const QString &firstPartyHost);

signals:
    void rulesChanged();
    void blockedCountChanged(int total);

private:
    static bool hostMatchesAllowed(const QString &host, const QString &allowed);

    AppConfig &m_config;
    AdblockBackend *m_backend = nullptr;   // owned; created per reload()
    mutable QReadWriteLock m_backendLock;  // guards the backend pointer swap

    // Snapshot of the config consulted by decide() (IO thread): reading
    // QSettings from the IO thread would be a data race.
    mutable QMutex m_policyMutex;
    bool m_enabled = true;
    QStringList m_allowlist;
    QStringList m_warnings;

    mutable QMutex m_statsMutex;
    int m_blockedTotal = 0;
    QHash<QString, int> m_blockedPerSite;
};
