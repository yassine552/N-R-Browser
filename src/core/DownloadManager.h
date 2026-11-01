// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

class AppConfig;
class QWebEngineDownloadRequest;

enum class DownloadState
{
    Requested,
    InProgress,
    Completed,
    Cancelled,
    Interrupted
};

// One active or finished download. Wraps QWebEngineDownloadRequest.
class DownloadItem : public QObject
{
    Q_OBJECT
public:
    explicit DownloadItem(QWebEngineDownloadRequest *request, QObject *parent = nullptr);

    QUrl url() const { return m_url; }
    QString fileName() const { return m_fileName; }
    QString directory() const { return m_directory; }
    QString filePath() const;
    DownloadState state() const { return m_state; }
    qint64 receivedBytes() const { return m_received; }
    qint64 totalBytes() const { return m_total; }
    QString interruptReasonText() const { return m_interruptReasonText; }

    // Called by DownloadManager after the destination has been decided.
    void startWithDestination(const QString &directory, const QString &fileName);
    void cancel();
    void openFile();

signals:
    void changed();

private:
    void setState(DownloadState s);
    void bindProgressSignals();

    QWebEngineDownloadRequest *m_request = nullptr; // owned by the profile
    QUrl m_url;
    QString m_fileName;
    QString m_directory;
    qint64 m_received = 0;
    qint64 m_total = 0;
    DownloadState m_state = DownloadState::Requested;
    QString m_interruptReasonText;
};

// Central download handling for all profiles. Decides destinations
// (user dialog or configured default), never silently overwrites files,
// and exposes the list for the Downloads interface.
class DownloadManager : public QObject
{
    Q_OBJECT
public:
    explicit DownloadManager(AppConfig &config, QObject *parent = nullptr);

    QVector<DownloadItem *> items() const { return m_items; }
    int activeCount() const;
    void clearFinished();

    // Finds an unused file name in the target directory
    // ("name (1).ext", "name (2).ext", ...).
    static QString uniqueFileName(const QString &directory, const QString &fileName);

public slots:
    // Slot for QWebEngineProfile::downloadRequested.
    void handleDownloadRequested(QWebEngineDownloadRequest *request);

signals:
    void itemsChanged();

private:
    AppConfig &m_config;
    QVector<DownloadItem *> m_items;
};
