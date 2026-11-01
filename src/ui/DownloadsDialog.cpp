// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "DownloadsDialog.h"

#include "BrowserApplication.h"
#include "DownloadManager.h"
#include "LanguageManager.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace std::chrono_literals;

DownloadsDialog::DownloadsDialog(BrowserApplication *app, QWidget *parent)
    : QDialog(parent)
    , m_app(app)
{
    setModal(true);
    setMinimumSize(620, 380);
    setWindowTitle(tr("Downloads"));

    auto *layout = new QVBoxLayout(this);

    auto *stack = new QStackedWidget(this);
    m_emptyLabel = new QLabel(tr("No downloads yet."), stack);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_list = new QListWidget(stack);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(false);
    stack->addWidget(m_emptyLabel);
    stack->addWidget(m_list);
    layout->addWidget(stack, 1);

    auto *buttons = new QWidget(this);
    auto *buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    m_openButton = new QPushButton(tr("Open"), buttons);
    m_cancelButton = new QPushButton(tr("Cancel"), buttons);
    m_clearButton = new QPushButton(tr("Clear finished"), buttons);
    connect(m_openButton, &QPushButton::clicked, this, &DownloadsDialog::openSelected);
    connect(m_cancelButton, &QPushButton::clicked, this, &DownloadsDialog::cancelSelected);
    connect(m_clearButton, &QPushButton::clicked, this, &DownloadsDialog::clearFinished);
    buttonsLayout->addWidget(m_openButton);
    buttonsLayout->addWidget(m_cancelButton);
    buttonsLayout->addStretch(1);
    buttonsLayout->addWidget(m_clearButton);
    layout->addWidget(buttons);

    connect(m_app->downloads(), &DownloadManager::itemsChanged, this, &DownloadsDialog::refresh);
    refresh();
}

void DownloadsDialog::refresh()
{
    const QVector<DownloadItem *> items = m_app->downloads()->items();
    const QLocale locale = m_app->languages()->locale();
    const int selected = m_list->currentRow();

    m_list->clear();
    for (DownloadItem *item : items) {
        QString state;
        switch (item->state()) {
        case DownloadState::Requested:
        case DownloadState::InProgress:
            state = tr("Downloading… %1")
                        .arg(locale.formattedDataSize(item->receivedBytes(), 1,
                                                      QLocale::DataSizeTraditionalFormat));
            break;
        case DownloadState::Completed:
            state = tr("Completed");
            break;
        case DownloadState::Cancelled:
            state = tr("Cancelled");
            break;
        case DownloadState::Interrupted:
            state = tr("Failed: %1").arg(item->interruptReasonText());
            break;
        }

        auto *row = new QWidget(m_list);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(6, 6, 6, 6);
        auto *topLine = new QLabel(QStringLiteral("<b>%1</b> — %2")
                                        .arg(item->fileName().toHtmlEscaped(), state),
                                    row);
        topLine->setTextFormat(Qt::RichText);
        auto *pathLine = new QLabel(QFileInfo(item->filePath()).absolutePath(), row);
        pathLine->setObjectName(QStringLiteral("downloadPath"));

        if (item->state() == DownloadState::InProgress
            || item->state() == DownloadState::Requested) {
            auto *bar = new QProgressBar(row);
            bar->setMaximumHeight(8);
            bar->setTextVisible(false);
            if (item->totalBytes() > 0) {
                bar->setRange(0, 100);
                bar->setValue(int(item->receivedBytes() * 100 / item->totalBytes()));
            } else {
                bar->setRange(0, 0); // indeterminate
            }
            rowLayout->addWidget(bar);
        }
        rowLayout->addWidget(topLine);
        rowLayout->addWidget(pathLine);

        QListWidgetItem *listItem = new QListWidgetItem(m_list);
        listItem->setData(Qt::UserRole, QVariant::fromValue(item));
        m_list->setItemWidget(listItem, row);
    }

    m_list->setVisible(!items.isEmpty());
    m_emptyLabel->setVisible(items.isEmpty());
    if (selected >= 0 && selected < m_list->count())
        m_list->setCurrentRow(selected);
}

void DownloadsDialog::openSelected()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    DownloadItem *download = item->data(Qt::UserRole).value<DownloadItem *>();
    if (download)
        download->openFile();
}

void DownloadsDialog::cancelSelected()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    DownloadItem *download = item->data(Qt::UserRole).value<DownloadItem *>();
    if (download && download->state() == DownloadState::InProgress)
        download->cancel();
}

void DownloadsDialog::clearFinished()
{
    m_app->downloads()->clearFinished();
}
