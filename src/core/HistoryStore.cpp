#include "HistoryStore.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

using namespace std::chrono_literals;

namespace {
constexpr const char *kConnectionName = "nor_history";
}

HistoryStore::HistoryStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
{
    m_connectionName = QString::fromLatin1(kConnectionName);
    m_valid = open(dbPath);
}

HistoryStore::~HistoryStore()
{
    if (m_valid)
        QSqlDatabase::database(m_connectionName, false).close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool HistoryStore::open(const QString &dbPath)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        m_lastError = db.lastError().text();
        qWarning("HistoryStore: open failed: %s", qPrintable(m_lastError));
        return false;
    }
    QSqlQuery pragma(db);
    pragma.prepare(QStringLiteral("PRAGMA journal_mode=WAL"));
    if (!pragma.exec())
        qWarning("HistoryStore: WAL pragma failed (non-fatal)");
    if (!exec(QStringLiteral(
              "CREATE TABLE IF NOT EXISTS history ("
              " id INTEGER PRIMARY KEY AUTOINCREMENT,"
              " url TEXT NOT NULL UNIQUE,"
              " title TEXT NOT NULL DEFAULT '',"
              " lastVisit INTEGER NOT NULL,"
              " visitCount INTEGER NOT NULL DEFAULT 1)"))) {
        return false;
    }
    if (!exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_lastVisit ON history(lastVisit)")))
        return false;
    return exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_url ON history(url)"));
}

bool HistoryStore::exec(const QString &sql)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    if (!query.exec(sql)) {
        m_lastError = query.lastError().text();
        qWarning("HistoryStore: exec failed: %s (%s)", qPrintable(sql), qPrintable(m_lastError));
        return false;
    }
    return true;
}

QString HistoryStore::escapeLike(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    out.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return out;
}

bool HistoryStore::addVisit(const QString &url, const QString &title)
{
    return addVisit(url, title, QDateTime::currentSecsSinceEpoch());
}

bool HistoryStore::addVisit(const QString &url, const QString &title, qint64 atEpochSecs)
{
    if (url.isEmpty())
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.transaction())
        qWarning("HistoryStore: could not start transaction");
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO history (url, title, lastVisit, visitCount) "
        "VALUES (:url, :title, :now, 1) "
        "ON CONFLICT(url) DO UPDATE SET "
        " title = CASE WHEN excluded.title != '' THEN excluded.title ELSE history.title END, "
        " lastVisit = MAX(history.lastVisit, excluded.lastVisit), "
        " visitCount = visitCount + 1"));
    query.bindValue(QStringLiteral(":url"), url);
    query.bindValue(QStringLiteral(":title"), title);
    query.bindValue(QStringLiteral(":now"), QVariant::fromValue<qint64>(atEpochSecs));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning("HistoryStore: addVisit failed: %s", qPrintable(m_lastError));
        db.rollback();
        return false;
    }
    db.commit();
    emit historyChanged();
    return true;
}

bool HistoryStore::updateTitleForUrl(const QString &url, const QString &title)
{
    if (url.isEmpty() || title.isEmpty())
        return false;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("UPDATE history SET title = :title WHERE url = :url"));
    query.bindValue(QStringLiteral(":title"), title);
    query.bindValue(QStringLiteral(":url"), url);
    if (!query.exec() || query.numRowsAffected() == 0)
        return false;
    emit historyChanged();
    return true;
}

QVector<HistoryEntry> HistoryStore::recent(int limit) const
{
    QVector<HistoryEntry> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "SELECT id, url, title, lastVisit, visitCount FROM history "
        "ORDER BY lastVisit DESC LIMIT :limit"));
    query.bindValue(QStringLiteral(":limit"), limit);
    if (!query.exec()) {
        qWarning("HistoryStore: recent failed: %s", qPrintable(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        HistoryEntry e;
        e.id = query.value(0).toLongLong();
        e.url = query.value(1).toString();
        e.title = query.value(2).toString();
        e.lastVisit = query.value(3).toLongLong();
        e.visitCount = query.value(4).toInt();
        result.append(e);
    }
    return result;
}

QVector<HistoryEntry> HistoryStore::mostVisited(int limit) const
{
    QVector<HistoryEntry> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "SELECT id, url, title, lastVisit, visitCount FROM history "
        "WHERE visitCount > 1 ORDER BY visitCount DESC, lastVisit DESC LIMIT :limit"));
    query.bindValue(QStringLiteral(":limit"), limit);
    if (!query.exec())
        return result;
    while (query.next()) {
        HistoryEntry e;
        e.id = query.value(0).toLongLong();
        e.url = query.value(1).toString();
        e.title = query.value(2).toString();
        e.lastVisit = query.value(3).toLongLong();
        e.visitCount = query.value(4).toInt();
        result.append(e);
    }
    return result;
}

QVector<HistoryEntry> HistoryStore::allEntries(int cap) const
{
    QVector<HistoryEntry> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "SELECT id, url, title, lastVisit, visitCount FROM history "
        "ORDER BY lastVisit DESC LIMIT :cap"));
    query.bindValue(QStringLiteral(":cap"), cap);
    if (!query.exec())
        return result;
    while (query.next()) {
        HistoryEntry e;
        e.id = query.value(0).toLongLong();
        e.url = query.value(1).toString();
        e.title = query.value(2).toString();
        e.lastVisit = query.value(3).toLongLong();
        e.visitCount = query.value(4).toInt();
        result.append(e);
    }
    return result;
}

QVector<HistoryEntry> HistoryStore::search(const QString &needle, int limit) const
{
    QVector<HistoryEntry> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "SELECT id, url, title, lastVisit, visitCount FROM history "
        "WHERE title LIKE :pattern ESCAPE '\\' OR url LIKE :pattern ESCAPE '\\' "
        "ORDER BY lastVisit DESC LIMIT :limit"));
    const QString pattern = QLatin1Char('%') + escapeLike(needle) + QLatin1Char('%');
    query.bindValue(QStringLiteral(":pattern"), pattern);
    query.bindValue(QStringLiteral(":limit"), limit);
    if (!query.exec()) {
        qWarning("HistoryStore: search failed: %s", qPrintable(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        HistoryEntry e;
        e.id = query.value(0).toLongLong();
        e.url = query.value(1).toString();
        e.title = query.value(2).toString();
        e.lastVisit = query.value(3).toLongLong();
        e.visitCount = query.value(4).toInt();
        result.append(e);
    }
    return result;
}

bool HistoryStore::removeEntry(qint64 id)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("DELETE FROM history WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qint64>(id));
    if (!query.exec() || query.numRowsAffected() == 0) {
        m_lastError = query.lastError().text();
        return false;
    }
    emit historyChanged();
    return true;
}

bool HistoryStore::removeByUrl(const QString &url)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("DELETE FROM history WHERE url = :url"));
    query.bindValue(QStringLiteral(":url"), url);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    emit historyChanged();
    return true;
}

bool HistoryStore::clearAll()
{
    if (!exec(QStringLiteral("DELETE FROM history")))
        return false;
    emit historyCleared();
    emit historyChanged();
    return true;
}

qint64 HistoryStore::count() const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM history")) || !query.next())
        return 0;
    return query.value(0).toLongLong();
}
