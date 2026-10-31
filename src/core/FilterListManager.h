#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class AdBlockClient;
class AppConfig;
class QNetworkAccessManager;
class QNetworkReply;

/*
 * FilterListManager: downloads and caches remote filter lists
 * (EasyList / EasyPrivacy by default) and maintains the serialized
 * engine cache for fast startup.
 *
 * Behaviour:
 *  - updateAll() re-downloads every configured list with
 *    If-None-Match/If-Modified-Since; 304 responses are a no-op.
 *  - Downloaded content is validated (see isPlausibleFilterList) before
 *    it replaces the cached copy; corrupt or truncated downloads are
 *    discarded and the previous cache stays in use.
 *  - Lists are stored under Paths::filterListsDir() with an atomic
 *    write (temporary file + rename), keyed by a hash of the URL.
 *  - Everything works fully offline: no cached list, no update, engine
 *    falls back to the built-in starter list.
 *  - After each successful reload the engine is serialized to
 *    "engine.cache" with a metadata sidecar recording input hashes;
 *    on startup a matching cache deserializes instead of re-parsing
 *    (measured ~50x faster than parsing on the full EasyList pair).
 *
 * The manager runs on the GUI thread; downloads are asynchronous.
 */
class FilterListManager : public QObject
{
    Q_OBJECT
public:
    explicit FilterListManager(AppConfig &config, AdBlockClient &client,
                               QObject *parent = nullptr);
    ~FilterListManager() override;

    // Loads the engine from cached lists/engine cache, or (re)builds it.
    // Call once at startup before any window is created.
    void initialize();

    // Starts asynchronous updates for all configured lists (when
    // auto-update is enabled). Emits listsChanged() when a list changed.
    void updateAll();

    // Manual update regardless of the auto-update setting (used by the
    // settings dialog "Update now" action).
    void updateNow();

    // Serializes the client's engine into the startup cache (atomic write,
    // with an input-hash sidecar). Safe to call whenever the engine was
    // rebuilt; no-op when the backend does not support caching.
    void saveEngineCache();

    QStringList warnings() const { return m_warnings; }

    // Path helpers exposed for tests.
    static QString cacheFileNameForUrl(const QUrl &url);
    static bool isPlausibleFilterList(const QByteArray &raw);

signals:
    void listsChanged();
    void updateFinished(int changed, int failed);

private:
    void startUpdate(const QUrl &url);
    void onReplyFinished(const QUrl &url, QNetworkReply *reply);
    bool loadEngineCacheIfValid();
    QStringList cachedListFiles() const;

    AppConfig &m_config;
    AdBlockClient &m_client;
    QNetworkAccessManager *m_nam = nullptr;
    QStringList m_warnings;
    int m_pendingReplies = 0;
    int m_changedThisRun = 0;
    int m_failedThisRun = 0;
};
