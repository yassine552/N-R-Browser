// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QDialog>

class BrowserApplication;
class QPushButton;
class QTableWidget;

// Bookmark manager: add, edit title/URL, remove, open.
class BookmarksDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BookmarksDialog(BrowserApplication *app, QWidget *parent = nullptr);

signals:
    void openUrlRequested(const QUrl &url);

private slots:
    void refresh();
    void addBookmark();
    void removeSelected();
    void openSelected();
    void onItemChanged(int row, int column);

private:
    BrowserApplication *m_app;
    QTableWidget *m_table = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_openButton = nullptr;
    bool m_loading = false;
};
