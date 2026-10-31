#include "AdBlockClient.h"

#include "AppConfig.h"
#include "BuiltInAdblockBackend.h"

#ifdef NOR_HAVE_RUST_ADBLOCK
#include "RustAdblockBackend.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QReadWriteLock>
#include <QUrl>

#include "Paths.h"

using namespace std::chrono_literals;

AdBlockClient::AdBlockClient(AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    // The backend is created in reload(); keep a BuiltIn instance until then
    // so decide() is safe to call at any time.
    m_backend = new BuiltInAdblockBackend();
}

AdBlockClient::~AdBlockClient()
{
    delete m_backend;
}

void AdBlockClient::refreshConfigCache()
{
    QMutexLocker locker(&m_policyMutex);
    m_enabled = m_config.adBlockEnabled();
    m_allowlist = m_config.adBlockAllowlist();
}

void AdBlockClient::reload()
{
    refreshConfigCache();

    AdblockBackend *backend = nullptr;
#ifdef NOR_HAVE_RUST_ADBLOCK
    backend = new RustAdblockBackend();
#else
    backend = new BuiltInAdblockBackend();
#endif

    m_warnings.clear();

    // Built-in starter list embedded in the application resources.
    QFile builtIn(QStringLiteral(":/filters/starter-blocklist.txt"));
    if (builtIn.open(QIODevice::ReadOnly | QIODevice::Text))
        backend->addList(QString::fromUtf8(builtIn.readAll()));
    else
        m_warnings << QStringLiteral("built-in list missing from resources");

    // User-supplied local lists.
    const QStringList custom = m_config.customFilterLists();
    for (const QString &path : custom) {
        QFile f(path);
        if (!f.exists()) {
            m_warnings << QStringLiteral("missing list: %1").arg(path);
            continue;
        }
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            backend->addList(QString::fromUtf8(f.readAll()));
        } else {
            m_warnings << QStringLiteral("unreadable list: %1").arg(path);
        }
    }

    // Downloaded lists cached by the filter-list manager (if present).
    // The manager itself lives in BrowserApplication; cached files under
    // Paths::filterListsDir() are read here so the engine works offline.
    const QDir listsDir(Paths::filterListsDir());
    const QStringList cached = listsDir.entryList(
        QStringList() << QStringLiteral("*.txt"), QDir::Files, QDir::Name);
    for (const QString &fileName : cached) {
        QFile f(listsDir.filePath(fileName));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            backend->addList(QString::fromUtf8(f.readAll()));
    }

    backend->build();

    {
        QWriteLocker locker(&m_backendLock);
        AdblockBackend *old = m_backend;
        m_backend = backend;
        delete old;
    }

    emit rulesChanged();
}

bool AdBlockClient::hostMatchesAllowed(const QString &host, const QString &allowed)
{
    if (host == allowed)
        return true;
    // Allow "example.com" to also cover "www.example.com".
    return host.endsWith(QLatin1Char('.') + allowed);
}

bool AdBlockClient::hostAllowed(const QString &host) const
{
    QMutexLocker locker(&m_policyMutex);
    for (const QString &allowed : m_allowlist)
        if (hostMatchesAllowed(host, allowed))
            return true;
    return false;
}

FilterDecision AdBlockClient::decide(const QUrl &requestUrl, const QUrl &firstPartyUrl,
                                     quint32 resourceType) const
{
    FilterDecision d;
    {
        QMutexLocker locker(&m_policyMutex);
        if (!m_enabled)
            return d;
    }

    const QString host = requestUrl.host().toLower();
    // 1) Per-site allowlist wins over everything.
    if (hostAllowed(host))
        return d;

    // 2) Engine decision (exception rules override blocking rules).
    QReadLocker locker(&m_backendLock);
    return m_backend->decide(requestUrl, firstPartyUrl, resourceType);
}

int AdBlockClient::blockRuleCount() const
{
    return m_backend->blockRuleCount();
}

int AdBlockClient::exceptionRuleCount() const
{
    return m_backend->exceptionRuleCount();
}

int AdBlockClient::skippedRuleCount() const
{
    return m_backend->skippedRuleCount();
}

int AdBlockClient::blockedTotal() const
{
    QMutexLocker locker(&m_statsMutex);
    return m_blockedTotal;
}

int AdBlockClient::blockedForSite(const QString &firstPartyHost) const
{
    QMutexLocker locker(&m_statsMutex);
    return m_blockedPerSite.value(firstPartyHost.toLower(), 0);
}

QStringList AdBlockClient::loadWarnings() const
{
    return m_warnings;
}

QString AdBlockClient::backendName() const
{
    return m_backend->name();
}

bool AdBlockClient::cosmeticFilteringSupported() const
{
    return m_backend->supportsCosmeticFiltering();
}

bool AdBlockClient::engineCacheSupported() const
{
    return m_backend->supportsEngineCache();
}

CosmeticResources AdBlockClient::cosmeticResources(const QUrl &pageUrl) const
{
    // Policy mirrors decide(): disabled engine and allow-listed sites
    // never apply cosmetic filters either.
    if (!m_config.adBlockEnabled())
        return CosmeticResources();
    if (hostAllowed(pageUrl.host().toLower()))
        return CosmeticResources();
    QReadLocker locker(&m_backendLock);
    return m_backend->cosmeticResources(pageUrl);
}

QStringList AdBlockClient::genericHideSelectors(const QStringList &classes,
                                                const QStringList &ids,
                                                const QStringList &exceptions) const
{
    if (!m_config.adBlockEnabled())
        return {};
    QReadLocker locker(&m_backendLock);
    return m_backend->genericHideSelectors(classes, ids, exceptions);
}

QByteArray AdBlockClient::serializeEngine() const
{
    return m_backend->serializeEngine();
}

bool AdBlockClient::deserializeEngine(const QByteArray &data)
{
    return m_backend->deserializeEngine(data);
}

void AdBlockClient::resetBlockedTotal()
{
    {
        QMutexLocker locker(&m_statsMutex);
        m_blockedTotal = 0;
        m_blockedPerSite.clear();
    }
    emit blockedCountChanged(0);
}

void AdBlockClient::noteBlocked()
{
    int total = 0;
    {
        QMutexLocker locker(&m_statsMutex);
        total = ++m_blockedTotal;
    }
    emit blockedCountChanged(total);
}

void AdBlockClient::noteBlockedForSite(const QString &firstPartyHost)
{
    int total = 0;
    {
        QMutexLocker locker(&m_statsMutex);
        total = ++m_blockedTotal;
        m_blockedPerSite[firstPartyHost.toLower()]++;
    }
    emit blockedCountChanged(total);
}
