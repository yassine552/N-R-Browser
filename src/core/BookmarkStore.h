#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct Bookmark
{
    qint64 id = 0;
    QString title;
    QString url;
    QString folder;
    qint64 addedAt = 0;
};

// SQLite-backed bookmark storage. All statements are prepared (bound
// values); no string concatenation is used to build queries.
class BookmarkStore : public QObject
{
    Q_OBJECT
public:
    // path may be ":memory:" for tests.
    explicit BookmarkStore(const QString &dbPath, QObject *parent = nullptr);
    ~BookmarkStore() override;

    bool addBookmark(const QString &title, const QString &url, const QString &folder = QString());
    bool updateBookmark(qint64 id, const QString &title, const QString &url);
    bool removeBookmark(qint64 id);
    bool removeBookmarkByUrl(const QString &url);

    QVector<Bookmark> bookmarks() const;             // ordered by addedAt
    bool containsUrl(const QString &url) const;
    Bookmark bookmarkForUrl(const QString &url) const;
    qint64 count() const;

    bool isValid() const { return m_valid; }
    QString lastError() const { return m_lastError; }

signals:
    void bookmarksChanged();

private:
    bool open(const QString &dbPath);
    bool exec(const QString &sql);

    bool m_valid = false;
    QString m_lastError;
    QString m_connectionName;
};
