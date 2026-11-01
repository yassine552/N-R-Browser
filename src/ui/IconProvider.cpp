// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "IconProvider.h"

#include "Theme.h"

#include <QFile>
#include <QHash>
#include <QMutex>
#include <QPainter>
#include <QSvgRenderer>

namespace {
QHash<QString, QIcon> &cache()
{
    static QHash<QString, QIcon> s_cache;
    return s_cache;
}
QMutex &cacheMutex()
{
    static QMutex s_mutex;
    return s_mutex;
}

// Icons whose internal colors are part of the brand mark and must never
// be re-tinted.
bool isBrandIcon(const QString &name)
{
    return name == QLatin1String("app");
}

// Every stock icon is a stroke SVG drawn with a placeholder stroke color.
// Rewriting that one color keeps the whole set homogeneous in BOTH themes
// (the old fixed #c9c2d8 only fit the purple-dark theme).
QByteArray recoloredSvg(const QString &name, const QColor &color)
{
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    QByteArray svg = file.readAll();
    const QByteArray from = QByteArrayLiteral("stroke='#c9c2d8'");
    const QByteArray from2 = QByteArrayLiteral("stroke=\"#c9c2d8\"");
    const QByteArray to = "stroke='" + color.name().toUtf8() + "'";
    const QByteArray to2 = "stroke=\"" + color.name().toUtf8() + "\"";
    svg.replace(from, to);
    svg.replace(from2, to2);
    return svg;
}

// Render the recolored SVG into a crisp QIcon with HiDPI pixmaps.
QIcon renderIcon(const QByteArray &svg, const QColor &base)
{
    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return QIcon();
    QIcon icon;
    // 1x and 2x pixmaps for the sizes the chrome actually requests.
    for (int size : {16, 18, 24, 32, 64}) {
        for (qreal dpr : {1.0, 2.0}) {
            const int px = qRound(size * dpr);
            QPixmap pixmap(px, px);
            pixmap.setDevicePixelRatio(dpr);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing, true);
            renderer.render(&painter, QRectF(0, 0, size, size));
            painter.end();
            icon.addPixmap(pixmap);
        }
    }
    Q_UNUSED(base);
    return icon;
}
} // namespace

QIcon IconProvider::icon(const QString &name)
{
    {
        QMutexLocker lock(&cacheMutex());
        const auto it = cache().constFind(name);
        if (it != cache().constEnd())
            return it.value();
    }

    QIcon icon;
    if (isBrandIcon(name)) {
        icon = QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
    } else {
        icon = renderIcon(recoloredSvg(name, Theme::palette().icon), Theme::palette().icon);
    }
    if (icon.isNull() || icon.availableSizes().isEmpty()) {
        // Fallback glyph so toolbars never show empty boxes.
        QPixmap fallback(24, 24);
        fallback.fill(Qt::transparent);
        icon.addPixmap(fallback);
    }
    QMutexLocker lock(&cacheMutex());
    cache().insert(name, icon);
    return icon;
}

QIcon IconProvider::appIcon()
{
    return icon(QStringLiteral("app"));
}

void IconProvider::clearCache()
{
    QMutexLocker lock(&cacheMutex());
    cache().clear();
}
