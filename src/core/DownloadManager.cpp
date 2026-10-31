#include "DownloadManager.h"

#include "AppConfig.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QWebEngineDownloadRequest>
#include <QWidget>

using namespace std::chrono_literals;

DownloadItem::DownloadItem(QWebEngineDownloadRequest *request, QObject *parent)
    : QObject(parent)
    , m_request(request)
{
    m_url = request->url();
    m_fileName = request->suggestedFileName();
    if (m_fileName.isEmpty()) {
        const QString path = request->url().path();
        m_fileName = path.section(QLatin1Char('/'), -1);
    }
    if (m_fileName.isEmpty())
        m_fileName = QStringLiteral("download");
    bindProgressSignals();
}

void DownloadItem::bindProgressSignals()
{
    connect(m_request, &QWebEngineDownloadRequest::receivedBytesChanged, this, [this]() {
        m_received = m_request->receivedBytes();
        emit changed();
    });
    connect(m_request, &QWebEngineDownloadRequest::totalBytesChanged, this, [this]() {
        m_total = m_request->totalBytes();
        emit changed();
    });
    connect(m_request, &QWebEngineDownloadRequest::stateChanged, this,
            [this](QWebEngineDownloadRequest::DownloadState s) {
                switch (s) {
                case QWebEngineDownloadRequest::DownloadRequested:
                    m_state = DownloadState::Requested;
                    break;
                case QWebEngineDownloadRequest::DownloadInProgress:
                    m_state = DownloadState::InProgress;
                    break;
                case QWebEngineDownloadRequest::DownloadCompleted:
                    m_state = DownloadState::Completed;
                    break;
                case QWebEngineDownloadRequest::DownloadCancelled:
                    m_state = DownloadState::Cancelled;
                    break;
                case QWebEngineDownloadRequest::DownloadInterrupted:
                    m_state = DownloadState::Interrupted;
                    m_interruptReasonText = m_request->interruptReasonString();
                    break;
                }
                emit changed();
            });
}

QString DownloadItem::filePath() const
{
    return QDir(m_directory).filePath(m_fileName);
}

void DownloadItem::startWithDestination(const QString &directory, const QString &fileName)
{
    m_directory = directory;
    m_fileName = fileName;
    m_request->setDownloadDirectory(directory);
    m_request->setDownloadFileName(fileName);
    m_request->accept();
    m_state = DownloadState::InProgress;
    emit changed();
}

void DownloadItem::cancel()
{
    if (m_state == DownloadState::InProgress || m_state == DownloadState::Requested)
        m_request->cancel();
}

void DownloadItem::openFile()
{
    if (m_state == DownloadState::Completed && !m_directory.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath()));
}

void DownloadItem::setState(DownloadState s)
{
    if (m_state != s) {
        m_state = s;
        emit changed();
    }
}

// ---------------------------------------------------------------------------

DownloadManager::DownloadManager(AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
}

int DownloadManager::activeCount() const
{
    int n = 0;
    for (const DownloadItem *i : m_items)
        if (i->state() == DownloadState::InProgress || i->state() == DownloadState::Requested)
            ++n;
    return n;
}

void DownloadManager::clearFinished()
{
    bool removed = false;
    for (int i = m_items.size() - 1; i >= 0; --i) {
        const DownloadState s = m_items.at(i)->state();
        if (s == DownloadState::Completed || s == DownloadState::Cancelled
            || s == DownloadState::Interrupted) {
            m_items.at(i)->deleteLater();
            m_items.removeAt(i);
            removed = true;
        }
    }
    if (removed)
        emit itemsChanged();
}

QString DownloadManager::uniqueFileName(const QString &directory, const QString &fileName)
{
    QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    QString candidate = fileName;
    int counter = 1;
    while (QFileInfo::exists(QDir(directory).filePath(candidate))) {
        candidate = suffix.isEmpty()
                        ? QStringLiteral("%1 (%2)").arg(base).arg(counter)
                        : QStringLiteral("%1 (%2).%3").arg(base).arg(counter).arg(suffix);
        ++counter;
    }
    return candidate;
}

void DownloadManager::handleDownloadRequested(QWebEngineDownloadRequest *request)
{
    qInfo("DownloadManager: download requested: %s",
          qPrintable(request->url().toString()));
    auto *item = new DownloadItem(request, this);

    QString directory = m_config.downloadDirectory();
    if (!QDir(directory).exists())
        directory = QDir::homePath();

    QString fileName = item->fileName();

    if (m_config.askWhereToSaveDownloads()) {
        // Modal dialog with the suggested name; the native dialog itself
        // asks before overwriting an existing file.
        QWidget *parent = QCoreApplication::instance()
                              ? QCoreApplication::instance()->property("norActiveWindow").value<QWidget *>()
                              : nullptr;
        const QString chosen = QFileDialog::getSaveFileName(
            parent, tr("Save File"), QDir(directory).filePath(fileName));
        if (chosen.isEmpty()) {
            request->cancel();
            delete item;
            return;
        }
        const QFileInfo chosenInfo(chosen);
        directory = chosenInfo.absolutePath();
        fileName = chosenInfo.fileName();
        // Honor the user's explicit choice even if the file exists.
    } else {
        fileName = uniqueFileName(directory, fileName);
    }

    item->startWithDestination(directory, fileName);
    m_items.append(item);
    emit itemsChanged();

    connect(item, &DownloadItem::changed, this, &DownloadManager::itemsChanged);
}
