// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "BookmarksDialog.h"

#include "BookmarkStore.h"
#include "BrowserApplication.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace std::chrono_literals;

BookmarksDialog::BookmarksDialog(BrowserApplication *app, QWidget *parent)
    : QDialog(parent)
    , m_app(app)
{
    setModal(true);
    setMinimumSize(640, 420);
    setWindowTitle(tr("Bookmarks"));

    auto *layout = new QVBoxLayout(this);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({ tr("Title"), tr("Address") });
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &BookmarksDialog::openSelected);
    connect(m_table, &QTableWidget::cellChanged, this, &BookmarksDialog::onItemChanged);

    auto *buttons = new QWidget(this);
    auto *buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    m_addButton = new QPushButton(tr("Add"), buttons);
    m_removeButton = new QPushButton(tr("Remove selected"), buttons);
    m_openButton = new QPushButton(tr("Open"), buttons);
    connect(m_addButton, &QPushButton::clicked, this, &BookmarksDialog::addBookmark);
    connect(m_removeButton, &QPushButton::clicked, this, &BookmarksDialog::removeSelected);
    connect(m_openButton, &QPushButton::clicked, this, &BookmarksDialog::openSelected);
    buttonsLayout->addWidget(m_addButton);
    buttonsLayout->addWidget(m_removeButton);
    buttonsLayout->addStretch(1);
    buttonsLayout->addWidget(m_openButton);

    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);

    connect(m_app->bookmarks(), &BookmarkStore::bookmarksChanged, this, &BookmarksDialog::refresh);
    refresh();
}

void BookmarksDialog::refresh()
{
    m_loading = true;
    const QVector<Bookmark> marks = m_app->bookmarks()->bookmarks();
    m_table->setRowCount(marks.size());
    for (int i = 0; i < marks.size(); ++i) {
        auto *title = new QTableWidgetItem(marks.at(i).title);
        auto *url = new QTableWidgetItem(marks.at(i).url);
        title->setData(Qt::UserRole, marks.at(i).id);
        url->setData(Qt::UserRole, marks.at(i).id);
        m_table->setItem(i, 0, title);
        m_table->setItem(i, 1, url);
    }
    m_loading = false;
}

void BookmarksDialog::addBookmark()
{
    m_app->bookmarks()->addBookmark(tr("New bookmark"), QStringLiteral("https://"));
    // The table refreshes through bookmarksChanged; start editing the title.
    const int last = m_table->rowCount() - 1;
    if (last >= 0) {
        m_table->editItem(m_table->item(last, 0));
    }
}

void BookmarksDialog::removeSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QTableWidgetItem *item = m_table->item(row, 0);
    const qint64 id = item->data(Qt::UserRole).toLongLong();
    m_app->bookmarks()->removeBookmark(id);
}

void BookmarksDialog::openSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QTableWidgetItem *item = m_table->item(row, 1);
    const QUrl url(item ? item->text() : QString());
    if (url.isValid() && !url.toString().isEmpty()) {
        emit openUrlRequested(url);
        accept();
    }
}

void BookmarksDialog::onItemChanged(int row, int column)
{
    if (m_loading)
        return;
    const QTableWidgetItem *idItem = m_table->item(row, column == 0 ? 0 : 1);
    const QTableWidgetItem *other = m_table->item(row, column == 0 ? 1 : 0);
    if (!idItem || !other)
        return;
    const qint64 id = idItem->data(Qt::UserRole).toLongLong();
    const QString title = column == 0 ? idItem->text() : other->text();
    const QString url = column == 1 ? idItem->text() : other->text();
    if (url.trimmed().isEmpty()) {
        QMessageBox::information(this, tr("Bookmarks"), tr("The address cannot be empty."));
        refresh();
        return;
    }
    m_app->bookmarks()->updateBookmark(id, title.trimmed(), url.trimmed());
}
