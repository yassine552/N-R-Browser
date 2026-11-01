// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QDialog>

class BrowserApplication;
class QLineEdit;
class QPushButton;
class QTableWidget;

// Browsing-history browser: search, open, delete selected, clear all.
class HistoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HistoryDialog(BrowserApplication *app, QWidget *parent = nullptr);

signals:
    void openUrlRequested(const QUrl &url);

private slots:
    void refresh();
    void openSelected();
    void removeSelected();
    void clearAll();

private:
    void keyPressEvent(QKeyEvent *event) override;

    BrowserApplication *m_app;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_clearButton = nullptr;
};
