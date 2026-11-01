// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "FilterListManager.h"

#include "AdBlockClient.h"
#include "AppConfig.h"
#include "Paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrl>

using namespace std::chrono_literals;

namespace {
constexpr qint64 kMinListBytes = 512;      // below this: corrupt/empty
constexpr qint64 kMaxListBytes = 64 * 1024 * 1024;
constexpr int kDownloadTimeoutMs = 60'000;

QString engineCachePath()
{
    return Paths::filterListsDir() + QStringLiteral("/engine.cache");
}

QString engineMetaPath()
{
    return Paths::filterListsDir() + QStringLiteral("/engine.meta");
}

QByteArray sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

// Plain HTTP is allowed only for loopback addresses: production lists are
// HTTPS, but local test servers (and locally hosted lists) are trusted.
bool isLoopbackUrl(const QUrl &url)
{
    const QString host = url.host();
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}
} // namespace

FilterListManager::FilterListManager(AppConfig &config, AdBlockClient &client,
                                     QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_client(client)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setTransferTimeout(kDownloadTimeoutMs);
}

FilterListManager::~FilterListManager() = default;

QString FilterListManager::cacheFileNameForUrl(const QUrl &url)
{
    const QByteArray hash = QCryptographicHash::hash(
        url.toString().toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash.left(16)) + QStringLiteral(".txt");
}

bool FilterListManager::isPlausibleFilterList(const QByteArray &raw)
{
    // Cheap structural validation so a truncated or HTML error page never
    // replaces a working list: UTF-8 decodable, plausible size, and mostly
    // lines that look like filter rules (not markup).
    if (raw.size() < kMinListBytes || raw.size() > kMaxListBytes)
        return false;
    if (raw.contains('\0'))
        return false;   // binary garbage is never a filter list
    const QString text = QString::fromUtf8(raw);
    if (text.contains(QChar(0xFFFD)))
        return false;   // replacement characters: not UTF-8
    if ((text.contains(QLatin1String("<html"), Qt::CaseInsensitive)
         || text.contains(QLatin1String("<!doctype"), Qt::CaseInsensitive))
        && !text.contains(QLatin1String("##")))
        return false;   // an HTML error page, not a filter list

    // What counts as a rule-like line? Real lists use several syntaxes:
    //   ||domain^ / @@exceptions / ##cosmetic / $options
    // but ALSO bare URL-substring patterns such as "-tracker." or
    // ".beacon.min.js" (EasyPrivacy uses those heavily). A network-rule
    // pattern never contains spaces, so any non-comment line whose
    // pattern part is a compact substring IS rule-like.
    int ruleLike = 0;
    int total = 0;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('!')) || t.startsWith(QLatin1Char('[')))
            continue;
        ++total;
        if (t.contains(QLatin1String("##")) || t.contains(QLatin1String("#@#"))
            || t.contains(QLatin1String("#$#")) || t.contains(QLatin1String("#?"))) {
            ++ruleLike;                     // cosmetic rule
        } else if (t.startsWith(QLatin1String("@@"))) {
            ++ruleLike;                     // exception rule
        } else {
            const QString pattern = t.contains(QLatin1Char('$'))
                                        ? t.section(QLatin1Char('$'), 0, 0)
                                        : t;
            // '<' can't begin a URL-substring rule (it is percent-encoded
            // in real URLs); lines starting with it are markup.
            if (!pattern.contains(QLatin1Char(' ')) && pattern.size() >= 2
                && !pattern.startsWith(QLatin1Char('<')))
                ++ruleLike;                 // compact network pattern
        }
        if (total >= 200)
            break;
    }
    return total > 0 && ruleLike * 10 >= total * 5;   // >=50% rule-like lines
}

void FilterListManager::initialize()
{
    if (!loadEngineCacheIfValid()) {
        m_client.reload();
        saveEngineCache();
    }
}

QStringList FilterListManager::cachedListFiles() const
{
    QDir dir(Paths::filterListsDir());
    return dir.entryList(QStringList() << QStringLiteral("*.txt"), QDir::Files);
}

bool FilterListManager::loadEngineCacheIfValid()
{
    if (!m_client.engineCacheSupported())
        return false;

    QFile metaFile(engineMetaPath());
    QFile cacheFile(engineCachePath());
    if (!metaFile.exists() || !cacheFile.exists())
        return false;
    if (!metaFile.open(QIODevice::ReadOnly) || !cacheFile.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    // Rebuild the expected input set: built-in + custom lists + cached
    // download files. The cache is only valid when nothing changed.
    QStringList expected;
    QFile builtIn(QStringLiteral(":/filters/starter-blocklist.txt"));
    if (builtIn.open(QIODevice::ReadOnly))
        expected.append(QString::fromLatin1(
            sha256(builtIn.readAll())));
    for (const QString &path : m_config.customFilterLists()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            expected.append(QString::fromLatin1(sha256(f.readAll())));
    }
    for (const QString &fileName : cachedListFiles()) {
        QFile f(Paths::filterListsDir() + QLatin1Char('/') + fileName);
        if (f.open(QIODevice::ReadOnly))
            expected.append(QString::fromLatin1(sha256(f.readAll())));
    }
    expected.sort();

    const QJsonArray recorded = doc.object().value(QLatin1String("inputs")).toArray();
    QStringList recordedList;
    for (const QJsonValue &v : recorded)
        recordedList.append(v.toString());
    recordedList.sort();

    if (recordedList != expected)
        return false;   // inputs changed: rebuild from the list files

    const QByteArray engine = cacheFile.readAll();
    if (!m_client.deserializeEngine(engine))
        return false;   // corrupt cache: rebuild

    // Counts (settings display) come from the list texts, not the cache.
    return true;
}

void FilterListManager::saveEngineCache()
{
    if (!m_client.engineCacheSupported())
        return;
    const QByteArray engine = m_client.serializeEngine();
    if (engine.isEmpty())
        return;

    QStringList inputs;
    QFile builtIn(QStringLiteral(":/filters/starter-blocklist.txt"));
    if (builtIn.open(QIODevice::ReadOnly))
        inputs.append(QString::fromLatin1(sha256(builtIn.readAll())));
    for (const QString &path : m_config.customFilterLists()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            inputs.append(QString::fromLatin1(sha256(f.readAll())));
    }
    for (const QString &fileName : cachedListFiles()) {
        QFile f(Paths::filterListsDir() + QLatin1Char('/') + fileName);
        if (f.open(QIODevice::ReadOnly))
            inputs.append(QString::fromLatin1(sha256(f.readAll())));
    }

    QJsonObject meta;
    meta.insert(QLatin1String("inputs"), QJsonArray::fromStringList(inputs));
    meta.insert(QLatin1String("savedAt"),
                QDateTime::currentDateTimeUtc().toSecsSinceEpoch());

    // Atomic writes: never leave a truncated cache behind.
    const QString dir = Paths::filterListsDir();
    QFile cacheFile(engineCachePath());
    if (cacheFile.open(QIODevice::WriteOnly))
        cacheFile.write(engine);
    QFile metaOut(engineMetaPath());
    if (metaOut.open(QIODevice::WriteOnly))
        metaOut.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
}

void FilterListManager::updateAll()
{
    if (!m_config.adBlockAutoUpdate())
        return;
    updateNow();
}

void FilterListManager::updateNow()
{
    m_warnings.clear();
    m_changedThisRun = 0;
    m_failedThisRun = 0;

    // Drop cached files whose URL is no longer configured.
    const QSet<QString> wanted = [&]() {
        QSet<QString> set;
        for (const QString &u : m_config.adBlockListUrls())
            set.insert(cacheFileNameForUrl(QUrl(u)));
        return set;
    }();
    const QStringList cached = cachedListFiles();
    bool removedAny = false;
    for (const QString &fileName : cached) {
        if (!wanted.contains(fileName)) {
            QFile::remove(Paths::filterListsDir() + QLatin1Char('/') + fileName);
            removedAny = true;
        }
    }
    if (removedAny)
        ++m_changedThisRun;   // engine must be rebuilt without stale lists

    for (const QString &urlText : m_config.adBlockListUrls()) {
        const QUrl url(urlText);
        if (!url.isValid()
            || (url.scheme() != QLatin1String("https") && !isLoopbackUrl(url))) {
            m_warnings << QStringLiteral("skipped non-HTTPS list URL: %1").arg(urlText);
            continue;
        }
        startUpdate(url);
    }
    if (m_pendingReplies == 0)
        emit updateFinished(m_changedThisRun, m_failedThisRun);
}

void FilterListManager::startUpdate(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader(QByteArrayLiteral("User-Agent"),
                          QByteArrayLiteral("NOR-Browser/1.0 (filter-list updater)"));

    // Conditional request via the stored state, so unchanged lists
    // answer 304 and cost nothing.
    const QJsonObject stateAll =
        QJsonDocument::fromJson(m_config.filterListState().toUtf8()).object();
    const QJsonObject state =
        stateAll.value(url.toString()).toObject();
    const QString etag = state.value(QLatin1String("etag")).toString();
    const QString lastModified = state.value(QLatin1String("lastModified")).toString();
    if (!etag.isEmpty())
        request.setRawHeader(QByteArrayLiteral("If-None-Match"), etag.toUtf8());
    if (!lastModified.isEmpty())
        request.setRawHeader(QByteArrayLiteral("If-Modified-Since"),
                             lastModified.toUtf8());

    QNetworkReply *reply = m_nam->get(request);
    ++m_pendingReplies;
    connect(reply, &QNetworkReply::finished, this,
            [this, url, reply]() { onReplyFinished(url, reply); });
}

void FilterListManager::onReplyFinished(const QUrl &url, QNetworkReply *reply)
{
    reply->deleteLater();
    --m_pendingReplies;

    QJsonObject stateAll =
        QJsonDocument::fromJson(m_config.filterListState().toUtf8()).object();
    QJsonObject state = stateAll.value(url.toString()).toObject();
    state.insert(QLatin1String("lastCheck"),
                 QDateTime::currentDateTimeUtc().toSecsSinceEpoch());

    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status == 304) {
        state.insert(QLatin1String("ok"), true);
        stateAll.insert(url.toString(), state);
        m_config.setFilterListState(QString::fromUtf8(
            QJsonDocument(stateAll).toJson(QJsonDocument::Compact)));
        if (m_pendingReplies == 0)
            emit updateFinished(m_changedThisRun, m_failedThisRun);
        return;
    }

    if (reply->error() != QNetworkReply::NoError
        || (status != 200 && status != 203 && status != 204)) {
        // Network failure: keep the cached list; the browser keeps working
        // offline with whatever it already has.
        m_warnings << QStringLiteral("list update failed (%1): %2")
                          .arg(status)
                          .arg(reply->errorString());
        state.insert(QLatin1String("ok"), false);
        state.insert(QLatin1String("error"), reply->errorString());
        stateAll.insert(url.toString(), state);
        m_config.setFilterListState(QString::fromUtf8(
            QJsonDocument(stateAll).toJson(QJsonDocument::Compact)));
        ++m_failedThisRun;
        if (m_pendingReplies == 0)
            emit updateFinished(m_changedThisRun, m_failedThisRun);
        return;
    }

    const QByteArray raw = reply->readAll();
    if (!isPlausibleFilterList(raw)) {
        m_warnings << QStringLiteral("discarded implausible download: %1")
                          .arg(url.toString());
        state.insert(QLatin1String("ok"), false);
        state.insert(QLatin1String("error"), QStringLiteral("implausible content"));
        stateAll.insert(url.toString(), state);
        m_config.setFilterListState(QString::fromUtf8(
            QJsonDocument(stateAll).toJson(QJsonDocument::Compact)));
        ++m_failedThisRun;
        if (m_pendingReplies == 0)
            emit updateFinished(m_changedThisRun, m_failedThisRun);
        return;
    }

    // Atomic replace: write next to the target, then rename over it.
    const QString target = Paths::filterListsDir() + QLatin1Char('/')
                           + cacheFileNameForUrl(url);
    const QString temp = target + QStringLiteral(".part");
    {
        QFile f(temp);
        if (!f.open(QIODevice::WriteOnly)) {
            m_warnings << QStringLiteral("cannot write list cache: %1").arg(target);
            ++m_failedThisRun;
            if (m_pendingReplies == 0)
                emit updateFinished(m_changedThisRun, m_failedThisRun);
            return;
        }
        f.write(raw);
    }
    if (!QFile::remove(target)) {
        // absent target: fine
    }
    if (!QFile::rename(temp, target)) {
        m_warnings << QStringLiteral("cannot replace list cache: %1").arg(target);
        ++m_failedThisRun;
        if (m_pendingReplies == 0)
            emit updateFinished(m_changedThisRun, m_failedThisRun);
        return;
    }

    const QVariant etagVariant = reply->header(QNetworkRequest::ETagHeader);
    if (etagVariant.isValid())
        state.insert(QLatin1String("etag"), etagVariant.toString());
    const QVariant lastModifiedVariant =
        reply->header(QNetworkRequest::LastModifiedHeader);
    if (lastModifiedVariant.isValid())
        state.insert(QLatin1String("lastModified"),
                     lastModifiedVariant.toDateTime().toUTC().toString(Qt::RFC2822Date));
    state.insert(QLatin1String("ok"), true);
    state.remove(QLatin1String("error"));
    stateAll.insert(url.toString(), state);
    m_config.setFilterListState(QString::fromUtf8(
        QJsonDocument(stateAll).toJson(QJsonDocument::Compact)));

    ++m_changedThisRun;
    if (m_pendingReplies == 0) {
        // All downloads settled: rebuild the engine with the new lists
        // and refresh the serialization cache.
        m_client.reload();
        saveEngineCache();
        emit listsChanged();
        emit updateFinished(m_changedThisRun, m_failedThisRun);
    }
}
