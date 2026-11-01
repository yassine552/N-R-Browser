// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QDialog>

class BrowserApplication;
class QCheckBox;
class QLabel;

// "Clear browsing data" dialog. Distinguishes data that can be wiped
// immediately from persistent site storage, which is removed on the next
// start (before the WebEngine profile is created).
class ClearDataDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ClearDataDialog(BrowserApplication *app, QWidget *parent = nullptr);

private slots:
    void clearNow();

private:
    BrowserApplication *m_app;
    QCheckBox *m_historyCheck = nullptr;
    QCheckBox *m_cookiesCheck = nullptr;
    QCheckBox *m_cacheCheck = nullptr;
    QCheckBox *m_storageCheck = nullptr;
    QLabel *m_resultLabel = nullptr;
};
