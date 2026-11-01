// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;
class QWebEnginePage;

// In-page find bar with next/previous navigation and a live match counter.
class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(QWidget *parent = nullptr);

    void attachPage(QWebEnginePage *page);
    void activate();
    bool isActive() const { return isVisible(); }
    void retranslateUi();

public slots:
    void findNext();
    void findPrevious();
    void hideBar();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void runFind(bool backward);

    QLineEdit *m_edit = nullptr;
    QPushButton *m_prev = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_close = nullptr;
    QLabel *m_counter = nullptr;
    QWebEnginePage *m_page = nullptr;
};
