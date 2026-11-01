// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "WebView.h"

#include "WebPage.h"

#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

using namespace std::chrono_literals;

WebView::WebView(QWidget *parent)
    : QWebEngineView(parent)
{
    // NOTE: render-process termination is handled at the page level
    // (QWebEnginePage::renderProcessTerminated) — wired in
    // TabWidget::newTab() once the page exists.
    connect(this, &QWebEngineView::loadStarted, this, [this]() { hideCrashOverlay(); });
}

void WebView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta > 0)
            zoomIn();
        else if (delta < 0)
            zoomOut();
        event->accept();
        return;
    }
    QWebEngineView::wheelEvent(event);
}

void WebView::contextMenuEvent(QContextMenuEvent *event)
{
    // The standard Chromium menu already handles link/tab/clipboard
    // actions and is localized with Qt's translations.
    QMenu *menu = createStandardContextMenu();
    if (menu) {
        menu->addSeparator();
        QAction *reload = menu->addAction(tr("Reload"));
        connect(reload, &QAction::triggered, this, &QWebEngineView::reload);
        QAction *back = menu->addAction(tr("Back"));
        connect(back, &QAction::triggered, this, &QWebEngineView::back);
        menu->exec(event->globalPos());
        menu->deleteLater();
    }
}

void WebView::zoomIn()
{
    setZoomFactor(qMin(qreal(5.0), zoomFactor() + 0.1));
    emit zoomChanged(zoomFactor());
}

void WebView::zoomOut()
{
    setZoomFactor(qMax(qreal(0.25), zoomFactor() - 0.1));
    emit zoomChanged(zoomFactor());
}

void WebView::zoomReset()
{
    setZoomFactor(1.0);
    emit zoomChanged(zoomFactor());
}

void WebView::showCrashOverlay()
{
    if (m_crashOverlay)
        return;
    m_crashOverlay = new QWidget(this);
    m_crashOverlay->setObjectName(QStringLiteral("crashOverlay"));
    auto *layout = new QVBoxLayout(m_crashOverlay);
    QLabel *label = new QLabel(tr("This page stopped responding.\nIts renderer process was closed."), m_crashOverlay);
    label->setAlignment(Qt::AlignCenter);
    QPushButton *reloadButton = new QPushButton(tr("Reload page"), m_crashOverlay);
    connect(reloadButton, &QPushButton::clicked, this, [this]() {
        hideCrashOverlay();
        this->reload();
    });
    layout->addStretch(1);
    layout->addWidget(label, 0, Qt::AlignCenter);
    layout->addWidget(reloadButton, 0, Qt::AlignCenter);
    layout->addStretch(1);
    m_crashOverlay->resize(size());
    m_crashOverlay->show();
    m_crashOverlay->raise();
}

void WebView::hideCrashOverlay()
{
    if (m_crashOverlay) {
        m_crashOverlay->deleteLater();
        m_crashOverlay = nullptr;
    }
}
