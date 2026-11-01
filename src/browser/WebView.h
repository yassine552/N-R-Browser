// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QWebEngineView>

// QWebEngineView with Ctrl+wheel zooming, the Chromium context menu
// (localized by Qt's own translations) plus a crash overlay.
class WebView : public QWebEngineView
{
    Q_OBJECT
public:
    explicit WebView(QWidget *parent = nullptr);

    qreal zoomLevel() const { return zoomFactor(); }
    void zoomIn();
    void zoomOut();
    void zoomReset();

signals:
    void zoomChanged(qreal factor);

public slots:
    void showCrashOverlay();
    void hideCrashOverlay();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QWidget *m_crashOverlay = nullptr;
};
