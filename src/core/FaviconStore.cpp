// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "FaviconStore.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QIODevice>
#include <QPainter>
#include <QPixmap>
#include <QSaveFile>

FaviconStore::FaviconStore(const QString &cacheDir, QObject *parent)
    : QObject(parent)
    , m_dir(cacheDir)
{
    QDir().mkpath(m_dir);
}

QString FaviconStore::sanitizeHost(const QString &host)
{
    if (host.isEmpty() || host.size() > 253)
        return QString();
    QString out;
    out.reserve(host.size());
    const QString lower = host.toLower();
    for (const QChar c : lower) {
        const ushort u = c.unicode();
        const bool okChar = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')
                            || u == '.' || u == '-';
        if (!okChar)
            return QString(); // IDN and friends: no icon, letter tile instead
        out.append(c);
    }
    if (out.contains(QLatin1String("..")))
        return QString(); // traversal guard
    if (out.startsWith(QLatin1Char('.')) || out.startsWith(QLatin1Char('-')))
        return QString();
    return out;
}

QString FaviconStore::pathFor(const QString &host) const
{
    return m_dir + QLatin1Char('/') + host + QStringLiteral(".png");
}

void FaviconStore::capture(const QIcon &icon, const QUrl &pageUrl)
{
    if (icon.isNull() || !pageUrl.isValid())
        return;
    const QString scheme = pageUrl.scheme();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return;
    const QString host = sanitizeHost(pageUrl.host());
    if (host.isEmpty())
        return;

    // NORMALIZATION: every stored icon becomes the same optical size —
    // rendered at 64px, aspect-preservingly fitted into a 30px box, centered
    // on a 36x36 transparent canvas. A 16px favicon no longer renders as a
    // blurry blow-up and a 64px icon no longer dwarfs its neighbours, so
    // the Top Sites grid and bookmark rows keep ONE consistent icon size
    // and baseline (the alignment bug this replaces).
    const QPixmap src = icon.pixmap(64, 64);
    if (src.isNull())
        return;
    QImage canvas(36, 36, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    const QPixmap fitted = src.scaled(30, 30, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
    QPainter painter(&canvas);
    painter.drawPixmap((36 - fitted.width()) / 2, (36 - fitted.height()) / 2, fitted);
    painter.end();

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!canvas.save(&buffer, "PNG"))
        return;
    QSaveFile f(pathFor(host));
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(bytes);
    f.commit();
}

bool FaviconStore::hasIcon(const QString &host) const
{
    const QString clean = sanitizeHost(host);
    if (clean.isEmpty())
        return false;
    return QFile::exists(pathFor(clean));
}

QByteArray FaviconStore::iconPng(const QString &host) const
{
    const QString clean = sanitizeHost(host);
    if (clean.isEmpty())
        return QByteArray();
    QFile f(pathFor(clean));
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

QString FaviconStore::iconUrlFor(const QString &host)
{
    return QStringLiteral("nor://favicon/") + sanitizeHost(host);
}

bool FaviconStore::clearAll()
{
    QDir dir(m_dir);
    if (!dir.exists())
        return true;
    const bool removed = dir.removeRecursively();
    // Recreate so later captures keep working in the same session.
    QDir().mkpath(m_dir);
    return removed;
}
