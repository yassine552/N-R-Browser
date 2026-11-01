// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "BookmarkStore.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

using namespace std::chrono_literals;

namespace {
constexpr const char *kConnectionName = "nor_bookmarks";
}

BookmarkStore::BookmarkStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
{
    m_connectionName = QString::fromLatin1(kConnectionName);
    m_valid = open(dbPath);
}

BookmarkStore::~BookmarkStore()
{
    if (m_valid)
        QSqlDatabase::database(m_connectionName, false).close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool BookmarkStore::open(const QString &dbPath)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        m_lastError = db.lastError().text();
        qWarning("BookmarkStore: open failed: %s", qPrintable(m_lastError));
        return false;
    }
    QSqlQuery pragma(db);
    pragma.prepare(QStringLiteral("PRAGMA journal_mode=WAL"));
    if (!pragma.exec()) {
        qWarning("BookmarkStore: WAL pragma failed (non-fatal)");
    }
    if (!exec(QStringLiteral(
              "CREATE TABLE IF NOT EXISTS bookmarks ("
              " id INTEGER PRIMARY KEY AUTOINCREMENT,"
              " title TEXT NOT NULL,"
              " url TEXT NOT NULL,"
              " folder TEXT NOT NULL DEFAULT '',"
              " addedAt INTEGER NOT NULL)"))) {
        return false;
    }
    return exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_bookmarks_url ON bookmarks(url)"));
}

bool BookmarkStore::exec(const QString &sql)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    if (!query.exec(sql)) {
        m_lastError = query.lastError().text();
        qWarning("BookmarkStore: exec failed: %s (%s)", qPrintable(sql), qPrintable(m_lastError));
        return false;
    }
    return true;
}

bool BookmarkStore::addBookmark(const QString &title, const QString &url, const QString &folder)
{
    if (url.isEmpty()) {
        m_lastError = QStringLiteral("empty url");
        return false;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "INSERT INTO bookmarks (title, url, folder, addedAt) VALUES (:title, :url, :folder, :added)"));
    query.bindValue(QStringLiteral(":title"), title.isEmpty() ? url : title);
    query.bindValue(QStringLiteral(":url"), url);
    // A default-constructed QString binds as SQL NULL; the column is NOT NULL.
    query.bindValue(QStringLiteral(":folder"), folder.isNull() ? QStringLiteral("") : folder);
    query.bindValue(QStringLiteral(":added"), QVariant::fromValue<qint64>(QDateTime::currentSecsSinceEpoch()));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning("BookmarkStore: add failed: %s", qPrintable(m_lastError));
        return false;
    }
    emit bookmarksChanged();
    return true;
}

bool BookmarkStore::updateBookmark(qint64 id, const QString &title, const QString &url)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("UPDATE bookmarks SET title = :title, url = :url WHERE id = :id"));
    query.bindValue(QStringLiteral(":title"), title);
    query.bindValue(QStringLiteral(":url"), url);
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qint64>(id));
    if (!query.exec() || query.numRowsAffected() == 0) {
        m_lastError = query.lastError().text();
        return false;
    }
    emit bookmarksChanged();
    return true;
}

bool BookmarkStore::removeBookmark(qint64 id)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("DELETE FROM bookmarks WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), QVariant::fromValue<qint64>(id));
    if (!query.exec() || query.numRowsAffected() == 0) {
        m_lastError = query.lastError().text();
        return false;
    }
    emit bookmarksChanged();
    return true;
}

bool BookmarkStore::removeBookmarkByUrl(const QString &url)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("DELETE FROM bookmarks WHERE url = :url"));
    query.bindValue(QStringLiteral(":url"), url);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    emit bookmarksChanged();
    return true;
}

QVector<Bookmark> BookmarkStore::bookmarks() const
{
    QVector<Bookmark> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("SELECT id, title, url, folder, addedAt FROM bookmarks ORDER BY addedAt ASC"));
    if (!query.exec()) {
        qWarning("BookmarkStore: select failed: %s", qPrintable(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        Bookmark b;
        b.id = query.value(0).toLongLong();
        b.title = query.value(1).toString();
        b.url = query.value(2).toString();
        b.folder = query.value(3).toString();
        b.addedAt = query.value(4).toLongLong();
        result.append(b);
    }
    return result;
}

bool BookmarkStore::containsUrl(const QString &url) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("SELECT 1 FROM bookmarks WHERE url = :url LIMIT 1"));
    query.bindValue(QStringLiteral(":url"), url);
    return query.exec() && query.next();
}

Bookmark BookmarkStore::bookmarkForUrl(const QString &url) const
{
    Bookmark b;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("SELECT id, title, url, folder, addedAt FROM bookmarks WHERE url = :url LIMIT 1"));
    query.bindValue(QStringLiteral(":url"), url);
    if (query.exec() && query.next()) {
        b.id = query.value(0).toLongLong();
        b.title = query.value(1).toString();
        b.url = query.value(2).toString();
        b.folder = query.value(3).toString();
        b.addedAt = query.value(4).toLongLong();
    }
    return b;
}

qint64 BookmarkStore::count() const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM bookmarks")) || !query.next())
        return 0;
    return query.value(0).toLongLong();
}
